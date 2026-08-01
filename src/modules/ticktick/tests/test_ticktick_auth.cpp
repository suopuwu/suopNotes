#include <catch2/catch_test_macros.hpp>

#include "TickTickAuth.h"

#include <QUrl>
#include <QUrlQuery>

// TickTick's OAuth is plain authorization-code with no PKCE and no refresh
// token, so the whole of the app's access hangs on getting one browser
// round-trip right. These specs pin the wire format — the parts that fail
// silently or with an error that does not say which side is wrong.

SCENARIO("The redirect URI is built so it can be registered verbatim", "[ticktick][auth]")
{
    GIVEN("a callback port")
    {
        THEN("the URI is loopback with the callback path")
        {
            // Must match what the user registered with TickTick byte-for-byte,
            // which is why the app derives it rather than asking them to retype
            // it.
            CHECK(TickTickAuth::redirectUriForPort(8080) == "http://localhost:8080/callback");
            CHECK(TickTickAuth::redirectUriForPort(9999) == "http://localhost:9999/callback");
        }
    }
}

SCENARIO("The authorize URL carries every parameter TickTick requires", "[ticktick][auth]")
{
    GIVEN("client id, redirect URI and state")
    {
        const QUrl url = TickTickAuth::buildAuthorizeUrl(
            "my-client", "http://localhost:8080/callback", "st4te");
        const QUrlQuery query(url);

        THEN("it points at TickTick's authorize endpoint")
        {
            CHECK(url.toString(QUrl::RemoveQuery) == "https://ticktick.com/oauth/authorize");
        }

        THEN("it requests an authorization code for the two available scopes")
        {
            CHECK(query.queryItemValue("response_type") == "code");
            CHECK(query.queryItemValue("client_id") == "my-client");
            CHECK(query.queryItemValue("state") == "st4te");
            CHECK(query.queryItemValue("scope", QUrl::FullyDecoded) == "tasks:read tasks:write");
        }

        THEN("the redirect URI survives encoding intact")
        {
            // It contains a colon and slashes; a URI that arrives mangled is
            // rejected by TickTick as a mismatch rather than as a bad URL.
            CHECK(query.queryItemValue("redirect_uri", QUrl::FullyDecoded)
                  == "http://localhost:8080/callback");
        }
    }
}

SCENARIO("Client credentials are sent as HTTP Basic, not as body parameters", "[ticktick][auth]")
{
    GIVEN("a client id and secret")
    {
        THEN("they are base64 of id:secret behind the Basic scheme")
        {
            // "abc:s3cret" -> YWJjOnMzY3JldA==
            CHECK(TickTickAuth::basicAuthHeader("abc", "s3cret")
                  == QByteArrayLiteral("Basic YWJjOnMzY3JldA=="));
        }
    }
}

SCENARIO("The token request body uses the only grant type TickTick accepts", "[ticktick][auth]")
{
    GIVEN("an authorization code")
    {
        const QByteArray body =
            TickTickAuth::buildTokenRequestBody("the-code", "http://localhost:8080/callback");
        const QUrlQuery parsed(QString::fromUtf8(body));

        THEN("it is an authorization_code exchange carrying the code and redirect URI")
        {
            // There is no refresh_token grant to fall back on — this is the
            // only exchange the integration ever performs.
            CHECK(parsed.queryItemValue("grant_type") == "authorization_code");
            CHECK(parsed.queryItemValue("code") == "the-code");
            CHECK(parsed.queryItemValue("redirect_uri", QUrl::FullyDecoded)
                  == "http://localhost:8080/callback");
        }
    }
}

SCENARIO("Only a genuine callback is accepted from the local listener", "[ticktick][auth]")
{
    GIVEN("the browser's redirect carrying a code and state")
    {
        const auto params = TickTickAuth::parseCallbackRequest(
            "GET /callback?code=abc123&state=st4te HTTP/1.1\r\nHost: localhost\r\n\r\n");

        THEN("the code and state are recovered")
        {
            CHECK(params.valid);
            CHECK(params.code == "abc123");
            CHECK(params.state == "st4te");
            CHECK(params.error.isEmpty());
        }
    }

    GIVEN("a redirect reporting that the user refused")
    {
        const auto params = TickTickAuth::parseCallbackRequest(
            "GET /callback?error=access_denied&state=st4te HTTP/1.1\r\n\r\n");

        THEN("it is a valid callback that carries the refusal")
        {
            // Valid, so the flow stops and reports rather than waiting forever
            // for a code that is never coming.
            CHECK(params.valid);
            CHECK(params.error == "access_denied");
            CHECK(params.code.isEmpty());
        }
    }

    GIVEN("a request for some other path")
    {
        const auto params =
            TickTickAuth::parseCallbackRequest("GET /favicon.ico HTTP/1.1\r\n\r\n");

        THEN("it is not treated as a callback")
        {
            // Browsers fetch favicons unprompted; treating that as an auth
            // result would abort a login that is still perfectly fine.
            CHECK_FALSE(params.valid);
        }
    }

    GIVEN("a request that is not a GET")
    {
        const auto params =
            TickTickAuth::parseCallbackRequest("POST /callback?code=abc HTTP/1.1\r\n\r\n");

        THEN("it is rejected")
        {
            CHECK_FALSE(params.valid);
        }
    }

    GIVEN("the callback path opened by hand, with no parameters")
    {
        const auto params = TickTickAuth::parseCallbackRequest("GET /callback HTTP/1.1\r\n\r\n");

        THEN("it is rejected rather than read as an empty code")
        {
            CHECK_FALSE(params.valid);
        }
    }

    GIVEN("garbage on the socket")
    {
        THEN("nothing is accepted")
        {
            CHECK_FALSE(TickTickAuth::parseCallbackRequest("").valid);
            CHECK_FALSE(TickTickAuth::parseCallbackRequest("hello").valid);
        }
    }

    GIVEN("a callback whose state is missing")
    {
        const auto params =
            TickTickAuth::parseCallbackRequest("GET /callback?code=abc123 HTTP/1.1\r\n\r\n");

        THEN("it parses, but with an empty state for the caller to reject")
        {
            // Parsing and state-checking are kept separate: the parser reports
            // what arrived, and the flow decides whether it matches the request
            // it actually made.
            CHECK(params.valid);
            CHECK(params.state.isEmpty());
        }
    }
}

SCENARIO("The token response is read strictly", "[ticktick][auth]")
{
    GIVEN("a successful response")
    {
        const auto response = TickTickAuth::parseTokenResponse(
            R"({"access_token":"tok","expires_in":15551999,"scope":"tasks:read tasks:write","token_type":"bearer"})");

        THEN("the token and its lifetime are taken")
        {
            CHECK(response.ok);
            CHECK(response.accessToken == "tok");
            // One second under 180 days — measured against the live service.
            CHECK(response.expiresInSeconds == 15551999);
        }
    }

    GIVEN("a response with no expires_in")
    {
        const auto response = TickTickAuth::parseTokenResponse(R"({"access_token":"tok"})");

        THEN("the token is still accepted, with an unknown lifetime")
        {
            // A token that works but cannot be warned about ahead of time is
            // still better than refusing to log in.
            CHECK(response.ok);
            CHECK(response.expiresInSeconds == 0);
        }
    }

    GIVEN("an OAuth error response")
    {
        const auto response = TickTickAuth::parseTokenResponse(
            R"({"error":"invalid_grant","error_description":"code expired"})");

        THEN("it fails with the human-readable description")
        {
            // The description says what actually went wrong; the HTTP status
            // alone would only say "Bad Request".
            CHECK_FALSE(response.ok);
            CHECK(response.error == "code expired");
        }
    }

    GIVEN("an error response with no description")
    {
        const auto response = TickTickAuth::parseTokenResponse(R"({"error":"invalid_client"})");

        THEN("the error code is used instead")
        {
            CHECK_FALSE(response.ok);
            CHECK(response.error == "invalid_client");
        }
    }

    GIVEN("a success-shaped response with no token in it")
    {
        const auto response = TickTickAuth::parseTokenResponse(R"({"token_type":"bearer"})");

        THEN("it fails rather than reporting an empty token as success")
        {
            CHECK_FALSE(response.ok);
            CHECK_FALSE(response.error.isEmpty());
        }
    }

    GIVEN("a response that is not JSON at all")
    {
        const auto response = TickTickAuth::parseTokenResponse("<html>502 Bad Gateway</html>");

        THEN("it fails with a reason rather than crashing")
        {
            CHECK_FALSE(response.ok);
            CHECK_FALSE(response.error.isEmpty());
        }
    }
}
