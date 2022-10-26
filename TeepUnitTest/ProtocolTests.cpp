// Copyright (c) Microsoft Corporation
// SPDX-License-Identifier: MIT
#include <filesystem>
#include <optional>
#include <sstream>
#include "catch.hpp"
#include "MockHttpTransport.h"
#include "qcbor/qcbor_decode.h"
#include "qcbor/qcbor_encode.h"
#include "qcbor/UsefulBuf.h"
#include "TeepAgentBrokerLib.h"
#include "TeepAgentLib.h"
#include "TeepTamBrokerLib.h"
#include "TeepTamLib.h"
#define TRUE 1
#define TAM_DATA_DIRECTORY "../../../tam"
#define TEEP_AGENT_DATA_DIRECTORY "../../../agent"
#define DEFAULT_TA_ID "38b08738-227d-4f6a-b1f0-b208bc02a781"
#define DEFAULT_TAM_URI "http://example.com/tam"

// Returns 0 on success, error on failure.
static int ConvertStringToUUID(_Out_ teep_uuid_t* uuid, _In_z_ const char* idString)
{
    const char* p = idString;
    int length = 0;
    int value;
    while (length < TEEP_UUID_SIZE) {
        if (*p == '-') {
            p++;
            continue;
        }
        if (sscanf_s(p, "%02x", &value) == 0) {
            return 1;
        }
        uuid->b[length++] = value;
        p += 2;
    }
    if (*p != 0) {
        return 1;
    }
    return 0;
}

static void CopyFile(
    _In_z_ const char* sourceFilename,
    _In_z_ const char* destinationDirectory)
{
    std::filesystem::path sourcePath = std::filesystem::path(sourceFilename);
    std::filesystem::path filename = sourcePath.filename();
    std::filesystem::path destinationPath = std::filesystem::path(destinationDirectory) /= filename;
    copy(sourceFilename, destinationPath, std::filesystem::copy_options::overwrite_existing);
}

static void ConfigureKeys()
{
    // Provision TAM key in TAM if not already done.
    char tam_public_key_filename[256];
    REQUIRE(StartTamBroker(TAM_DATA_DIRECTORY, TRUE, tam_public_key_filename) == 0);

    // Provision Agent key in agent if not already done.
    char agent_public_key_filename[256];
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, agent_public_key_filename) == 0);

    // Copy Agent keys to TAM
    CopyFile(agent_public_key_filename, TAM_DATA_DIRECTORY "/trusted");

    // Copy TAM keys to Agent
    CopyFile(tam_public_key_filename, TEEP_AGENT_DATA_DIRECTORY "/trusted");

    StopAgentBroker();
    StopTamBroker();
}

TEST_CASE("UnrequestTA", "[protocol]")
{
    ConfigureKeys();
    REQUIRE(StartTamBroker(TAM_DATA_DIRECTORY, TRUE, nullptr) == 0);
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    teep_uuid_t unneededTaid;
    int err = ConvertStringToUUID(&unneededTaid, DEFAULT_TA_ID);
    REQUIRE(err == 0);
    teep_error_code_t teep_error = TeepAgentUnrequestTA(unneededTaid, DEFAULT_TAM_URI);
    REQUIRE(teep_error == TEEP_ERR_SUCCESS);

    // Verify 2 messages sent (QueryRequest, QueryResponse).
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1 + 2);

    StopAgentBroker();
    StopTamBroker();
}

TEST_CASE("RequestTA", "[protocol]")
{
    ConfigureKeys();
    REQUIRE(StartTamBroker(TAM_DATA_DIRECTORY, TRUE, nullptr) == 0);
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    teep_uuid_t requestedTaid;
    int err = ConvertStringToUUID(&requestedTaid, DEFAULT_TA_ID);
    REQUIRE(err == 0);
    teep_error_code_t teep_error = TeepAgentRequestTA(requestedTaid, DEFAULT_TAM_URI);
    REQUIRE(teep_error == TEEP_ERR_SUCCESS);

    // Verify 4 messages sent (QueryRequest, QueryResponse, Update, Success).
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1 + 4);

    StopAgentBroker();
    StopTamBroker();
}

TEST_CASE("PolicyCheck with no policy change", "[protocol]")
{
    ConfigureKeys();
    REQUIRE(StartTamBroker(TAM_DATA_DIRECTORY, TRUE, nullptr) == 0);
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    teep_error_code_t teep_error = TeepAgentRequestPolicyCheck(DEFAULT_TAM_URI);
    REQUIRE(teep_error == TEEP_ERR_SUCCESS);

    // Verify 4 messages sent (QueryRequest, QueryResponse, Update, Success).
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1 + 4);

    StopAgentBroker();
    StopTamBroker();
}

// TODO: implement a test for a PolicyCheck when there is a policy change.

TEST_CASE("Unexpected ProcessError", "[protocol]")
{
    ConfigureKeys();
    REQUIRE(StartTamBroker(TAM_DATA_DIRECTORY, TRUE, nullptr) == 0);
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    teep_error_code_t teep_error = TeepAgentProcessError(nullptr);
    REQUIRE(teep_error == TEEP_ERR_TEMPORARY_ERROR);

    // Verify no messages sent.
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1);

    StopAgentBroker();
    StopTamBroker();
}

TEST_CASE("RequestPolicyCheck errors", "[protocol]")
{
    ConfigureKeys();
    REQUIRE(StartTamBroker(TAM_DATA_DIRECTORY, TRUE, nullptr) == 0);
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    // Schedule a transport error during each of the 3 operations:
    // Connect, QueryRequest, QueryResponse.
    for (int count = 1; count <= 3; count++) {
        uint64_t counter1 = GetOutboundMessagesSent();
        ScheduleTransportError(count);

        teep_error_code_t teep_error = TeepAgentRequestPolicyCheck(DEFAULT_TAM_URI);
        REQUIRE(teep_error == TEEP_ERR_TEMPORARY_ERROR);

        // Verify the correct number of messages were sent.
        uint64_t counter2 = GetOutboundMessagesSent();
        REQUIRE(counter2 == counter1 + count - 1);
    }

    StopAgentBroker();
    StopTamBroker();
}

TEST_CASE("Agent receives bad media type", "[protocol]")
{
    ConfigureKeys();
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    // Try bad media type.
    void* sessionHandle = nullptr;
    std::string message = "hello";
    teep_error_code_t teep_error = TeepAgentProcessTeepMessage(
        sessionHandle, "mediaType", message.c_str(), message.size());
    REQUIRE(teep_error == TEEP_ERR_PERMANENT_ERROR);

    // Silent drop.  Verify no message sent.
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1);

    StopAgentBroker();
}

TEST_CASE("Agent receives bad COSE message", "[protocol]")
{
    ConfigureKeys();
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    // Try bad COSE message.
    void* sessionHandle = nullptr;
    std::string message = "hello";
    teep_error_code_t teep_error = TeepAgentProcessTeepMessage(
        sessionHandle, TEEP_CBOR_MEDIA_TYPE, message.c_str(), message.size());
    REQUIRE(teep_error == TEEP_ERR_PERMANENT_ERROR);

    // Silent drop.  Verify no message sent.
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1);

    StopAgentBroker();
}

teep_error_code_t
TamSignCborMessage(
    _In_ const UsefulBufC* unsignedMessage,
    _In_ UsefulBuf signedMessageBuffer,
    _Out_ UsefulBufC* signedMessage);

TEST_CASE("Agent receives bad TEEP message", "[protocol]")
{
    ConfigureKeys();
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    // Compose a bad TEEP message.
    std::string message = "hello";
    UsefulBufC unsignedMessage;
    unsignedMessage.ptr = message.c_str();
    unsignedMessage.len = message.size();
    UsefulBufC signedMessage;
    UsefulBuf_MAKE_STACK_UB(signedMessageBuffer, 300);
    teep_error_code_t teep_error = TamSignCborMessage(&unsignedMessage, signedMessageBuffer, &signedMessage);
    REQUIRE(teep_error == TEEP_ERR_SUCCESS);

    // Try bad COSE message.
    void* sessionHandle = nullptr;
    teep_error = TeepAgentProcessTeepMessage(
        sessionHandle, TEEP_CBOR_MEDIA_TYPE, (const char*)signedMessage.ptr, signedMessage.len);
    REQUIRE(teep_error == TEEP_ERR_PERMANENT_ERROR);

    // Verify that an Error message was sent.
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1 + 1);

    StopAgentBroker();
}

teep_error_code_t TamComposeQueryRequest(
    std::optional<int> minVersion,
    std::optional<int> maxVersion,
    _Out_ UsefulBufC* bufferToSend);

teep_error_code_t
TeepAgentSignCborMessage(
    _In_ const UsefulBufC* unsignedMessage,
    _In_ UsefulBuf signedMessageBuffer,
    _Out_ UsefulBufC* signedMessage);

teep_error_code_t TeepAgentComposeQueryResponse(
    _In_ QCBORDecodeContext* decodeContext,
    _Out_ UsefulBufC* encodedResponse,
    _Out_ UsefulBufC* errorResponse);

static void TestQueryRequestVersion(int min_version, int max_version, teep_error_code_t expected_result, uint64_t expected_message_count)
{
    ConfigureKeys();
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    // Compose a TEEP QueryRequest with an unsupported version.
    UsefulBuf_MAKE_STACK_UB(encoded, 4096);
    UsefulBufC unsignedMessage = UsefulBuf_Const(encoded);
    teep_error_code_t teep_error = TamComposeQueryRequest(min_version, max_version, &unsignedMessage);
    UsefulBufC signedMessage;
    UsefulBuf_MAKE_STACK_UB(signedMessageBuffer, 300);
    teep_error = TamSignCborMessage(&unsignedMessage, signedMessageBuffer, &signedMessage);
    REQUIRE(teep_error == TEEP_ERR_SUCCESS);

    void* sessionHandle = nullptr;
    teep_error = TeepAgentProcessTeepMessage(
        sessionHandle, TEEP_CBOR_MEDIA_TYPE, (const char*)signedMessage.ptr, signedMessage.len);
    REQUIRE(teep_error == expected_result);

    // Verify that the right number of messages were sent.
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1 + expected_message_count);

    StopAgentBroker();
}

TEST_CASE("Agent receives QueryRequest with supported version", "[protocol]")
{
    const uint64_t expected_message_count = 3; // QueryResponse, Update, Success.
    TestQueryRequestVersion(0, 0, TEEP_ERR_SUCCESS, expected_message_count);
}

TEST_CASE("Agent receives QueryRequest with supported and unsupported version", "[protocol]")
{
    const uint64_t expected_message_count = 3; // QueryResponse, Update, Success.
    TestQueryRequestVersion(0, 1, TEEP_ERR_SUCCESS, expected_message_count);
}

TEST_CASE("Agent receives QueryRequest with unsupported version", "[protocol]")
{
    const uint64_t expected_message_count = 1; // Error.
    TestQueryRequestVersion(1, 1, TEEP_ERR_UNSUPPORTED_MSG_VERSION, expected_message_count);
}

static teep_error_code_t TestComposeQueryResponse(int version, _Out_ UsefulBufC* encodedResponse)
{
    UsefulBufC challenge = NULLUsefulBufC;
    *encodedResponse = NULLUsefulBufC;
    UsefulBufC errorToken = NULLUsefulBufC;
    std::ostringstream errorMessage;

    size_t maxBufferLength = 4096;
    char* rawBuffer = (char*)malloc(maxBufferLength);
    if (rawBuffer == nullptr) {
        return TEEP_ERR_TEMPORARY_ERROR;
    }

    QCBOREncodeContext context;
    UsefulBuf buffer{ rawBuffer, maxBufferLength };
    QCBOREncode_Init(&context, buffer);

    QCBOREncode_OpenArray(&context);
    {
        // Add TYPE.
        QCBOREncode_AddInt64(&context, TEEP_MESSAGE_QUERY_RESPONSE);

        QCBOREncode_OpenMap(&context);
        {
            QCBOREncode_AddInt64ToMapN(&context, TEEP_LABEL_SELECTED_VERSION, version);
        }
        QCBOREncode_CloseMap(&context);
    }
    QCBOREncode_CloseArray(&context);

    UsefulBufC const_buffer = UsefulBuf_Const(buffer);
    QCBORError err = QCBOREncode_Finish(&context, &const_buffer);
    if (err != QCBOR_SUCCESS) {
        return TEEP_ERR_TEMPORARY_ERROR;
    }

    *encodedResponse = const_buffer;
    return TEEP_ERR_SUCCESS;
}

static void TestQueryResponseVersion(int version, teep_error_code_t expected_result, uint64_t expected_message_count)
{
    ConfigureKeys();
    REQUIRE(StartAgentBroker(TEEP_AGENT_DATA_DIRECTORY, TRUE, nullptr) == 0);

    uint64_t counter1 = GetOutboundMessagesSent();

    // Compose a TEEP QueryResponse with an unsupported version.
    UsefulBuf_MAKE_STACK_UB(encoded, 4096);
    UsefulBufC unsignedMessage = UsefulBuf_Const(encoded);
    teep_error_code_t teep_error = TestComposeQueryResponse(version, &unsignedMessage);
    UsefulBufC signedMessage;
    UsefulBuf_MAKE_STACK_UB(signedMessageBuffer, 300);
    teep_error = TeepAgentSignCborMessage(&unsignedMessage, signedMessageBuffer, &signedMessage);
    REQUIRE(teep_error == TEEP_ERR_SUCCESS);

    void* sessionHandle = nullptr;
    teep_error = TamProcessTeepMessage(
        sessionHandle, TEEP_CBOR_MEDIA_TYPE, (const char*)signedMessage.ptr, signedMessage.len);
    REQUIRE(teep_error == expected_result);

    // Verify that the right number of messages were sent.
    uint64_t counter2 = GetOutboundMessagesSent();
    REQUIRE(counter2 == counter1 + expected_message_count);

    StopAgentBroker();
}

TEST_CASE("TAM receives QueryResponse with supported version", "[protocol]")
{
    const uint64_t expected_message_count = 0;
    TestQueryResponseVersion(0, TEEP_ERR_SUCCESS, expected_message_count);
}

TEST_CASE("TAM receives QueryResponse with unsupported version", "[protocol]")
{
    const uint64_t expected_message_count = 0;
    TestQueryResponseVersion(1, TEEP_ERR_UNSUPPORTED_MSG_VERSION, expected_message_count);
}