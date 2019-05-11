/**
 * @file ExtensionTests.cpp
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

namespace {

    struct FooExtension
        : public Smtp::Client::Extension
    {
        // Properties

        std::string parameters;
        bool wasReset = false;

        // Smtp::Client::Extension

        virtual void Configure(const std::string& parameters) override {
            this->parameters = parameters;
        }

        virtual void Reset() override {
            wasReset = true;
        }

        virtual std::string ModifyMessage(
            const Smtp::Client::MessageContext& context,
            const std::string& input
        ) override {
            if (input.substr(0, 4) == "MAIL") {
                return input + " foo=bar";
            } else {
                return input;
            }
        }
    };

    struct BarPreMessageExtension
        : public Smtp::Client::Extension
    {
        // Properties

        bool performedExtraStage = false;
        std::function< void(const std::string& data) > onSendMessage;
        std::function< void(bool success) > onStageComplete;

        // Smtp::Client::Extension

        virtual std::string ModifyMessage(
            const Smtp::Client::MessageContext& context,
            const std::string& input
        ) override {
            return input;
        }

        virtual bool IsExtraProtocolStageNeededHere(
            const Smtp::Client::MessageContext& context
        ) override {
            if (
                performedExtraStage
                || (context.protocolStage != Smtp::Client::ProtocolStage::ReadyToSend)
            ) {
                return false;
            }
            performedExtraStage = true;
            return true;
        }

        virtual void GoAhead(
            std::function< void(const std::string& data) > onSendMessage,
            std::function< void(bool success) > onStageComplete
        ) override {
            this->onSendMessage = onSendMessage;
            this->onStageComplete = onStageComplete;
            onSendMessage("PogChamp\r\n");
        }

        virtual bool HandleServerMessage(
            const Smtp::Client::MessageContext& context,
            const Smtp::Client::ParsedMessage& message
        ) override {
            if (message.code != 250) {
                return false;
            }
            onStageComplete(true);
            return true;
        }
    };

    struct BarAfterSenderDeclaredExtension
        : public Smtp::Client::Extension
    {
        // Properties

        bool softFailureOnServerMessage = false;
        bool performedExtraStage = false;
        std::function< void(const std::string& data) > onSendMessage;
        std::function< void(bool success) > onStageComplete;

        // Smtp::Client::Extension

        virtual std::string ModifyMessage(
            const Smtp::Client::MessageContext& context,
            const std::string& input
        ) override {
            return input;
        }

        virtual bool IsExtraProtocolStageNeededHere(
            const Smtp::Client::MessageContext& context
        ) override {
            if (
                performedExtraStage
                || (context.protocolStage != Smtp::Client::ProtocolStage::DeclaringSender)
            ) {
                return false;
            }
            performedExtraStage = true;
            return true;
        }

        virtual void GoAhead(
            std::function< void(const std::string& data) > onSendMessage,
            std::function< void(bool success) > onStageComplete
        ) override {
            this->onSendMessage = onSendMessage;
            this->onStageComplete = onStageComplete;
        }

        virtual bool HandleServerMessage(
            const Smtp::Client::MessageContext& context,
            const Smtp::Client::ParsedMessage& message
        ) override {
            onStageComplete(!softFailureOnServerMessage);
            return true;
        }
    };

}

namespace SmtpTests {

    /**
     * This is the test fixture for these tests, providing common
     * setup and teardown for each test.
     */
    struct ExtensionTests
        : public Common
    {
    };

    TEST_F(ExtensionTests, ExtensionProtocolStageSuccess) {
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        const auto extension = std::make_shared< BarPreMessageExtension >();
        client.RegisterExtension("BAR", extension);
        ASSERT_TRUE(EstablishConnectionPrepareToSend(false));
        EXPECT_FALSE(FutureReady(readyOrBroken, std::chrono::milliseconds(100)));
        auto& connection = *clients[0].connection;
        auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "PogChamp\r\n",
            }),
            messages
        );
        SendTextMessage(connection, "250 OK\r\n");
        EXPECT_TRUE(FutureReady(readyOrBroken, std::chrono::milliseconds(1000)));
        EXPECT_TRUE(readyOrBroken.get());
        readyOrBroken = client.GetReadyOrBrokenFuture();
        MessageHeaders::MessageHeaders headers;
        headers.AddHeader("From", "<alex@example.com>");
        const std::string body = (
            "Hello, World!"
        );
        auto sendWasCompleted = client.SendMail(headers, body);
        messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "MAIL FROM:<alex@example.com>\r\n",
            }),
            messages
        );
        EXPECT_FALSE(FutureReady(readyOrBroken));
    }

    TEST_F(ExtensionTests, ExtensionProtocolStageHardFailure) {
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        const auto extension = std::make_shared< BarPreMessageExtension >();
        client.RegisterExtension("BAR", extension);
        ASSERT_TRUE(EstablishConnectionPrepareToSend(false));
        EXPECT_FALSE(FutureReady(readyOrBroken, std::chrono::milliseconds(100)));
        auto& connection = *clients[0].connection;
        auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "PogChamp\r\n",
            }),
            messages
        );
        SendTextMessage(connection, "535 Go away\r\n");
        EXPECT_TRUE(FutureReady(readyOrBroken, std::chrono::milliseconds(1000)));
        EXPECT_FALSE(readyOrBroken.get());
    }

    TEST_F(ExtensionTests, ExtensionSoftFailureOnServerMessage) {
        const auto extension = std::make_shared< BarAfterSenderDeclaredExtension >();
        extension->softFailureOnServerMessage = true;
        client.RegisterExtension("BAR", extension);
        ASSERT_TRUE(EstablishConnectionPrepareToSend());
        auto& connection = *clients[0].connection;
        MessageHeaders::MessageHeaders headers;
        headers.AddHeader("From", "<alex@example.com>");
        headers.AddHeader("To", "<bob@example.com>");
        const std::string body = (
            "Hello, World!"
        );
        auto sendWasCompleted = client.SendMail(headers, body);
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "MAIL FROM:<alex@example.com>\r\n",
            }),
            messages
        );
        EXPECT_FALSE(FutureReady(readyOrBroken, std::chrono::milliseconds(100)));
        EXPECT_FALSE(FutureReady(sendWasCompleted, std::chrono::milliseconds(100)));
        SendTextMessage(connection, "250 OK\r\n");
        EXPECT_TRUE(FutureReady(readyOrBroken, std::chrono::milliseconds(1000)));
        EXPECT_TRUE(readyOrBroken.get());
        EXPECT_TRUE(FutureReady(sendWasCompleted, std::chrono::milliseconds(1000)));
        EXPECT_FALSE(sendWasCompleted.get());
    }

    TEST_F(ExtensionTests, SupportedExtensionGetsToModifyMessagesInAnyStage) {
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        const auto extension = std::make_shared< FooExtension >();
        client.RegisterExtension("FOO", extension);
        ASSERT_TRUE(EstablishConnectionPrepareToSend());
        auto& connection = *clients[0].connection;
        MessageHeaders::MessageHeaders headers;
        headers.AddHeader("From", "<alex@example.com>");
        headers.AddHeader("To", "<bob@example.com>");
        const std::string body = (
            "Hello, World!"
        );
        auto sendWasCompleted = client.SendMail(headers, body);
        auto messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "MAIL FROM:<alex@example.com> foo=bar\r\n",
            }),
            messages
        );
        SendTextMessage(connection, "250 OK\r\n");
        messages = AwaitMessages(0, 1);
        EXPECT_EQ(
            std::vector< std::string >({
                "RCPT TO:<bob@example.com>\r\n",
            }),
            messages
        );
        EXPECT_TRUE(FutureReady(readyOrBroken));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ExtensionTests, UnsupportedExtensionDoesNotGetToModifyMessagesInAnyStage) {
        auto readyOrBroken = client.GetReadyOrBrokenFuture();
        const auto extension = std::make_shared< FooExtension >();
        client.RegisterExtension("SPAM", extension);
        ASSERT_TRUE(EstablishConnectionPrepareToSend());
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
        EXPECT_TRUE(FutureReady(readyOrBroken));
        EXPECT_TRUE(readyOrBroken.get());
    }

    TEST_F(ExtensionTests, SupportedExtensionGivenParameters) {
        const auto extension = std::make_shared< FooExtension >();
        client.RegisterExtension("FOO", extension);
        ASSERT_TRUE(EstablishConnectionPrepareToSend());
        EXPECT_EQ("Poggers", extension->parameters);
    }

    TEST_F(ExtensionTests, ExtensionResetAtStart) {
        const auto extension = std::make_shared< FooExtension >();
        client.RegisterExtension("FOO", extension);
        StartServer(false);
        ASSERT_TRUE(EstablishConnection(false));
        EXPECT_TRUE(extension->wasReset);
    }

}
