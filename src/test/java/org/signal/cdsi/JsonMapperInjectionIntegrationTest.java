package org.signal.cdsi;

import static org.junit.jupiter.api.Assertions.assertNotNull;

import io.micronaut.json.JsonMapper;
import io.micronaut.test.extensions.junit5.annotation.MicronautTest;
import jakarta.inject.Inject;
import org.junit.jupiter.api.Test;

@MicronautTest
public class JsonMapperInjectionIntegrationTest {

  @Inject
  JsonMapper jsonMapper;

  @Test
  void testJsonMapper() {
    // This won't actually fail - the failure will happen at injection time
    assertNotNull(jsonMapper, "a json mapper must be available for injection");
  }

}
