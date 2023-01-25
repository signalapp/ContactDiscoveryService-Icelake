/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.client;

import com.google.common.base.Preconditions;
import com.southernstorm.noise.protocol.CipherStatePair;
import com.southernstorm.noise.protocol.HandshakeState;
import io.micronaut.http.MediaType;
import io.micronaut.http.annotation.Consumes;
import io.micronaut.websocket.CloseReason;
import io.micronaut.websocket.annotation.ClientWebSocket;
import io.micronaut.websocket.annotation.OnClose;
import io.micronaut.websocket.annotation.OnMessage;
import io.micronaut.websocket.annotation.OnOpen;
import java.io.ByteArrayOutputStream;
import java.util.Arrays;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.signal.cdsi.proto.ClientHandshakeStart;
import org.signal.cdsi.proto.ClientRequest;
import org.signal.cdsi.proto.ClientResponse;

@ClientWebSocket
public abstract class CdsiWebsocket implements AutoCloseable {

  private static final String HANDSHAKE_PATTERN = "Noise_NK_25519_ChaChaPoly_SHA256";

  private HandshakeState handshakeState;
  private CipherStatePair cipherStatePair;

  private static final byte[] STREAM_OPENED = new byte[0];
  private static final byte[] STREAM_CLOSED = new byte[0];
  private final BlockingQueue<byte[]> incomingMessages = new LinkedBlockingQueue<>();
  private final AtomicReference<CloseReason> closeReason = new AtomicReference<>();

  @OnOpen
  public void onOpen() throws Exception {
    incomingMessages.put(STREAM_OPENED);
  }

  @OnMessage(maxPayloadLength = 10 << 20)
  @Consumes(MediaType.APPLICATION_OCTET_STREAM)
  public void onMessage(final byte[] encryptedMessage) throws Exception {
    incomingMessages.put(encryptedMessage);
  }

  @OnClose
  public void onClose(CloseReason reason) throws Exception {
    closeReason.set(reason);
    incomingMessages.put(STREAM_CLOSED);
  }

  protected abstract void send(byte[] message);

  public static final class CloseException extends Exception {
    private final CloseReason closeReason;

    CloseException(final CloseReason closeReason) {
      super("closed");
      this.closeReason = closeReason;
    }

    public CloseReason getReason() { return closeReason; }
  }

  private byte[] getNext() throws CloseException {
    byte[] next;
    try {
      next = incomingMessages.poll(10, TimeUnit.SECONDS);
    } catch (InterruptedException e) {
      throw new AssertionError("interrupted");
    }
    if (next == null)
      throw new AssertionError("null next (timeout?)");
    if (next == STREAM_CLOSED)
      throw new CloseException(closeReason.get());
    if (next == STREAM_OPENED)
      throw new AssertionError("opened");
    return next;
  }

  public static final int KEY_SIZE = 32;

  byte[] publicKeyFromHandshakeStart(byte[] clientHandshakeStart) throws Exception {
    ClientHandshakeStart start = ClientHandshakeStart.parseFrom(clientHandshakeStart);
    if (start.getTestOnlyPubkey().size() != KEY_SIZE) {
      throw new IllegalAccessException("invalid key size received from enclave in proto");
    }
    // test environments don't have attestation, so just return the public key
    return start.getTestOnlyPubkey().toByteArray();
  }

  public ClientResponse run(ClientRequest request) throws Exception {
    if (handshakeState != null)
      throw new AssertionError("run called twice");
    if (STREAM_OPENED != incomingMessages.take())
      throw new AssertionError("stream open not first");
    byte[] clientHandshakeStart = getNext();
    byte[] pubkey = publicKeyFromHandshakeStart(clientHandshakeStart);
    Preconditions.checkState(pubkey.length == 32);

    handshakeState = new HandshakeState(HANDSHAKE_PATTERN, HandshakeState.INITIATOR);
    handshakeState.getRemotePublicKey().setPublicKey(pubkey, 0);
    handshakeState.start();
    final byte[] handshakeMessage = new byte[64];
    final int handshakeMessageLength;

    final byte[] handshakePayload = new byte[]{}; // empty payload for handshake
    handshakeMessageLength = handshakeState.writeMessage(
        handshakeMessage, 0, handshakePayload, 0, handshakePayload.length);
    send(Arrays.copyOfRange(handshakeMessage, 0, handshakeMessageLength));

    byte[] handshake = getNext();
    handshakeState.readMessage(handshake, 0, handshake.length, new byte[0], 0);

    cipherStatePair = handshakeState.split();

    sendClientRequest(request);
    ClientResponse tokenResponse = getClientResponse();
    if (tokenResponse.getToken().isEmpty())
      throw new AssertionError("no token");

    sendClientRequest(ClientRequest.newBuilder().setTokenAck(true).build());
    ClientResponse finalResponse = getClientResponse();
    try {
      getNext();
      throw new AssertionError("received message after final response");
    } catch (CloseException e) {
      // Set the token in the response we send to the caller, so they can make subsequent requests.
      return finalResponse.toBuilder()
          .setToken(tokenResponse.getToken())
          .build();
    }
  }

  private static final int PACKET_SIZE = 65535;
  private static final int PACKET_INTEGRITY_SIZE = 16;
  private static final int PACKET_DATA_SIZE = PACKET_SIZE - PACKET_INTEGRITY_SIZE;

  private void sendClientRequest(ClientRequest request) throws Exception {
    final byte[] msg = request.toByteArray();
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    byte[] buf = new byte[PACKET_SIZE];
    for (int i = 0; i < msg.length; i += PACKET_DATA_SIZE) {
      int len = Math.min(msg.length - i, PACKET_DATA_SIZE);
      final int cipherTextLength = cipherStatePair.getSender()
          .encryptWithAd(null, msg, i, buf, 0, len);
      Preconditions.checkState(cipherTextLength == len + PACKET_INTEGRITY_SIZE);
      out.write(buf, 0, cipherTextLength);
    }
    send(out.toByteArray());
  }

  private ClientResponse getClientResponse() throws Exception {
    byte[] msg = getNext();
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    byte[] buf = new byte[PACKET_DATA_SIZE];
    for (int i = 0; i < msg.length; i += PACKET_SIZE) {
      int len = Math.min(msg.length - i, PACKET_SIZE);
      final int plainTextLength = cipherStatePair.getReceiver()
          .decryptWithAd(null, msg, i, buf, 0, len);
      Preconditions.checkState(plainTextLength == len - PACKET_INTEGRITY_SIZE);
      out.write(buf, 0, plainTextLength);
    }
    return ClientResponse.parseFrom(out.toByteArray());
  }
}
