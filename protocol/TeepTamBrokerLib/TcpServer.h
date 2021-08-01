// Copyright (c) Microsoft Corporation
// SPDX-License-Identifier: MIT
#pragma once

int StartTcpServer(void);
void StopTcpServer(void);
void AcceptTcpSession(void);
int HandleTcpMessage(void);
void CloseTcpSession(void);
