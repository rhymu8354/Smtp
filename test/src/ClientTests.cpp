/**
 * @file ClientTests.cpp
 *
 * This module contains the unit tests of the Smtp::Client class.
 *
 * © 2019 by Richard Walters
 */

#include "Common.hpp"

#include <algorithm>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <MessageHeaders/MessageHeaders.hpp>
#include <Smtp/Client.hpp>
#include <stdint.h>
#include <string>
#include <SystemAbstractions/NetworkEndpoint.hpp>
#include <TlsDecorator/TlsDecorator.hpp>
#include <vector>

namespace SmtpTests {

    /**
     * This is the test fixture for these tests, providing common
     * setup and teardown for each test.
     */
    struct ClientTests
        : public Common
    {
    };

    TEST_F(ClientTests, ConnectToServerWithTlsGoodCertificate) {
        StartServer(true);
        client.EnableTls(testGoodCertificate);
        auto connectionDidComplete = client.Connect(
            "alex.example.com",
            "localhost",
            serverPort
        );
        ASSERT_EQ(
            std::future_status::ready,
            connectionDidComplete.wait_for(std::chrono::milliseconds(1000))
        );
        const auto success = connectionDidComplete.get();
        EXPECT_TRUE(success);
        ASSERT_TRUE(AwaitConnections(1));
    }

    TEST_F(ClientTests, ConnectToServerWithTlsBadCertificate) {
        StartServer(true);
        client.EnableTls(testBadCertificate);
        auto connectionDidComplete = client.Connect(
            "alex.example.com",
            "localhost",
            serverPort
        );
        ASSERT_EQ(
            std::future_status::ready,
            connectionDidComplete.wait_for(std::chrono::milliseconds(1000))
        );
        const auto success = connectionDidComplete.get();
        EXPECT_FALSE(success);
    }

    TEST_F(ClientTests, ConnectToServerWithoutTls) {
        StartServer(false);
        auto connectionDidComplete = client.Connect(
            "alex.example.com",
            "localhost",
            serverPort
        );
        ASSERT_EQ(
            std::future_status::ready,
            connectionDidComplete.wait_for(std::chrono::milliseconds(1000))
        );
        const auto success = connectionDidComplete.get();
        EXPECT_TRUE(success);
        ASSERT_TRUE(AwaitConnections(1));
    }

    TEST_F(ClientTests, GreetingSuccess) {
        StartServer(false);
        ASSERT_TRUE(EstablishConnection(false));
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(
            connection,
            "220 mail.example.com Simple Mail Transfer Service Ready\r\n"
        );
        const auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "EHLO alex.example.com\r\n",
            }),
            messages
        );
        EXPECT_FALSE(FutureReady(readyOrBroken));
    }

    TEST_F(ClientTests, GreetingFailure) {
        StartServer(false);
        ASSERT_TRUE(EstablishConnection(false));
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(
            connection,
            "554 Go away you silly person\r\n"
        );
        EXPECT_TRUE(FutureReady(readyOrBroken, std::chrono::milliseconds(1000)));
    }

    TEST_F(ClientTests, OptionsSuccess) {
        StartServer(false);
        ASSERT_TRUE(EstablishConnection(false));
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(
            connection,
            "220 mail.example.com Simple Mail Transfer Service Ready\r\n"
        );
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250-mail.example.com greets alex.example.com\r\n");
        SendTextMessage(connection, "250-8BITMIME\r\n");
        SendTextMessage(connection, "250-SIZE\r\n");
        SendTextMessage(connection, "250-DSN\r\n");
        EXPECT_FALSE(FutureReady(readyOrBroken, std::chrono::milliseconds(100)));
        SendTextMessage(connection, "250 HELP\r\n");
        EXPECT_TRUE(FutureReady(readyOrBroken, std::chrono::milliseconds(1000)));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ClientTests, OptionsFailure) {
        StartServer(false);
        ASSERT_TRUE(EstablishConnection(false));
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(
            connection,
            "220 mail.example.com Simple Mail Transfer Service Ready\r\n"
        );
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "550 Go away, you smell\r\n");
        EXPECT_TRUE(FutureReady(readyOrBroken, std::chrono::milliseconds(1000)));
        EXPECT_FALSE(readyOrBroken.get());
    }

    TEST_F(ClientTests, SendMailFromSent) {
        ASSERT_TRUE(EstablishConnectionPrepareToSend());
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        MessageHeaders::MessageHeaders headers;
        headers.AddHeader("From", "<alex@example.com>");
        const std::string body = (
            "Hello, World!"
        );
        auto sendWasCompleted = client.SendMail(headers, body);
        const auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "MAIL FROM:<alex@example.com>\r\n",
            }),
            messages
        );
        EXPECT_FALSE(FutureReady(sendWasCompleted));
        EXPECT_FALSE(FutureReady(readyOrBroken));
    }

    TEST_F(ClientTests, SendMailFromAccepted) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n");
        const auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "RCPT TO:<bob@example.com>\r\n",
            }),
            messages
        );
        EXPECT_FALSE(FutureReady(sendWasCompleted, std::chrono::milliseconds(100)));
        EXPECT_FALSE(FutureReady(readyOrBroken));
    }

    TEST_F(ClientTests, SendMailFromRejected) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "550 Go away, you smell\r\n");
        EXPECT_TRUE(FutureReady(sendWasCompleted, std::chrono::milliseconds(1000)));
        EXPECT_FALSE(sendWasCompleted.get());
        EXPECT_TRUE(FutureReady(readyOrBroken));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ClientTests, SendMailFirstRecipientAccepted) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n"); // response to MAIL FROM:<alex@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<bob@example.com>
        const auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "RCPT TO:<carol@example.com>\r\n",
            }),
            messages
        );
        EXPECT_FALSE(FutureReady(sendWasCompleted, std::chrono::milliseconds(100)));
        EXPECT_FALSE(FutureReady(readyOrBroken));
    }

    TEST_F(ClientTests, SendMailFirstRecipientRejected) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n"); // response to MAIL FROM:<alex@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "550 No such user here\r\n"); // response to RCPT TO:<bob@example.com>
        EXPECT_TRUE(FutureReady(sendWasCompleted, std::chrono::milliseconds(1000)));
        EXPECT_FALSE(sendWasCompleted.get());
        EXPECT_TRUE(FutureReady(readyOrBroken));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ClientTests, SendMailAllRecipientsAccepted) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n"); // response to MAIL FROM:<alex@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<bob@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<carol@example.com>
        const auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "DATA\r\n",
            }),
            messages
        );
        EXPECT_FALSE(FutureReady(sendWasCompleted, std::chrono::milliseconds(100)));
        EXPECT_FALSE(FutureReady(readyOrBroken));
    }

    TEST_F(ClientTests, SendMailSecondRecipientsRejected) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n"); // response to MAIL FROM:<alex@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<bob@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "550 No such user\r\n"); // response to RCPT TO:<carol@example.com>
        EXPECT_TRUE(FutureReady(sendWasCompleted, std::chrono::milliseconds(1000)));
        EXPECT_FALSE(sendWasCompleted.get());
        EXPECT_TRUE(FutureReady(readyOrBroken));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ClientTests, SendMailDataGoAhead) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n"); // response to MAIL FROM:<alex@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<bob@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<carol@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "354 Start mail input; end with <CRLF>.<CRLF>\r\n"); // response to DATA
        const auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "From: <alex@example.com>\r\n",
                "To: <bob@example.com>\r\n",
                "To: <carol@example.com>\r\n",
                "Subject: food.exe\r\n",
                "\r\n",
                "Have you heard of food.exe?  admEJ\r\n",
                "That was a great game!\r\n",
                ".\r\n",
            }),
            messages
        );
        EXPECT_FALSE(FutureReady(sendWasCompleted, std::chrono::milliseconds(100)));
        EXPECT_FALSE(FutureReady(readyOrBroken));
    }

    TEST_F(ClientTests, SendMailDataNoGoAhead) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n"); // response to MAIL FROM:<alex@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<bob@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<carol@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "500 Go away, you smell\r\n"); // response to DATA
        ASSERT_TRUE(FutureReady(sendWasCompleted, std::chrono::milliseconds(1000)));
        EXPECT_FALSE(sendWasCompleted.get());
        EXPECT_TRUE(FutureReady(readyOrBroken));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ClientTests, SendMailDataAccepted) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n"); // response to MAIL FROM:<alex@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<bob@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<carol@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "354 Start mail input; end with <CRLF>.<CRLF>\r\n"); // response to DATA
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to headers/body
        ASSERT_TRUE(FutureReady(sendWasCompleted, std::chrono::milliseconds(1000)));
        EXPECT_TRUE(sendWasCompleted.get());
        EXPECT_TRUE(FutureReady(readyOrBroken));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ClientTests, SendMailDataRejected) {
        auto sendWasCompleted = StartSendingEmail();
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto& connection = *clients[0].connection;
        SendTextMessage(connection, "250 OK\r\n"); // response to MAIL FROM:<alex@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<bob@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "250 OK\r\n"); // response to RCPT TO:<carol@example.com>
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "354 Start mail input; end with <CRLF>.<CRLF>\r\n"); // response to DATA
        (void)AwaitMessages(0, 1);
        SendTextMessage(connection, "500 Go away, you smell\r\n"); // response to headers/body
        ASSERT_TRUE(FutureReady(sendWasCompleted, std::chrono::milliseconds(1000)));
        EXPECT_FALSE(sendWasCompleted.get());
        EXPECT_TRUE(FutureReady(readyOrBroken));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ClientTests, EscapeLineInBodyConsistingOfOnlyAFullStop) {
        // TODO
    }

    TEST_F(ClientTests, BodyNotExplicitlyEndingInANewLine) {
        // TODO
    }

}
