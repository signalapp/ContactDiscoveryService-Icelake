/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */
package org.signal.lambda;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.amazonaws.services.lambda.runtime.Context;
import com.amazonaws.services.lambda.runtime.events.DynamodbEvent;
import com.amazonaws.services.lambda.runtime.tests.EventLoader;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.jupiter.api.Test;
import org.mockito.ArgumentCaptor;
import software.amazon.awssdk.core.SdkBytes;
import software.amazon.awssdk.services.kinesis.KinesisClient;
import software.amazon.awssdk.services.kinesis.model.PutRecordRequest;

// Modeled after https://aws.amazon.com/blogs/opensource/testing-aws-lambda-functions-written-in-java/
class FilterCdsUpdatesHandlerTest {

  private static final byte[] uuidBytes = uuidToBytes(UUID.fromString("22222222-2222-2222-2222-222222222222"));
  private static final byte[] pniBytes = uuidToBytes(UUID.fromString("11111111-1111-1111-1111-111111111111"));

  static byte[] uuidToBytes(UUID uuid) {
    if (uuid == null) return null;
    ByteBuffer b = ByteBuffer.allocate(16);
    b.putLong(uuid.getMostSignificantBits());
    b.putLong(uuid.getLeastSignificantBits());
    return b.array();
  }

  void fileInputOutput(String filename, List<Account> expected) {
    DynamodbEvent event = EventLoader.loadDynamoDbEvent(filename);
    KinesisClient mockClient = mock(KinesisClient.class);
    FilterCdsUpdatesHandler handler = new FilterCdsUpdatesHandler(mockClient, "mystream");
    Context contextMock = mock(Context.class);
    handler.handleRequest(event, contextMock);
    ArgumentCaptor<PutRecordRequest> captor = ArgumentCaptor.forClass(PutRecordRequest.class);
    verify(mockClient, times(expected.size())).putRecord(captor.capture());
    List<Account> accounts = captor.getAllValues().stream().map(c -> mapWithoutException(c.data()))
        .collect(Collectors.toList());
    assertEquals(expected, accounts);
  }

  @Test
  void testE164Change() {
    fileInputOutput("testevent_numberchange.json",
        List.of(
            new Account("+12223334444", uuidBytes, false, pniBytes, null),
            new Account("+13334445555", uuidBytes, true, pniBytes, null)));
  }

  @Test
  void testUAKChange() {
    fileInputOutput("testevent_uakchange.json",
        List.of(
            new Account("+12223334444", uuidBytes, true, pniBytes,
                new byte[]{0x04, 0x10, 0x41, 0x04, 0x10, 0x41, 0x04, 0x10, 0x41, 0x04, 0x10, 0x41, 0x04, 0x10, 0x41, 0x04})));
  }

  @Test
  void testPNIChange() {
    fileInputOutput("testevent_pnichange.json",
        List.of(
            new Account("+12223334444", uuidBytes, true, pniBytes, null)));
  }

  Account mapWithoutException(SdkBytes in) {
    try {
      return FilterCdsUpdatesHandler.OBJECT_MAPPER.readValue(in.asInputStream(), Account.class);
    } catch (IOException e) {
      throw new RuntimeException("mapping", e);
    }
  }
}
