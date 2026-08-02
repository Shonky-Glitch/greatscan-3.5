#include <cstring>

#include "project_config.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <unity.h>

void test_project_config_values() {
  TEST_ASSERT_NOT_NULL(ProjectConfig::kDeviceName);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, std::strlen(ProjectConfig::kDeviceName));

  TEST_ASSERT_NOT_NULL(ProjectConfig::kStaSsid);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, std::strlen(ProjectConfig::kStaSsid));

  TEST_ASSERT_NOT_NULL(ProjectConfig::kStaPassword);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(8u, std::strlen(ProjectConfig::kStaPassword));

  TEST_ASSERT_NOT_NULL(ProjectConfig::kGreatScanStatusUrl);
  TEST_ASSERT_NOT_NULL(std::strstr(ProjectConfig::kGreatScanStatusUrl, "/api/status"));

  TEST_ASSERT_NOT_NULL(ProjectConfig::kGreatScanRawUrl);
  TEST_ASSERT_NOT_NULL(std::strstr(ProjectConfig::kGreatScanRawUrl, "/api/raw"));

  TEST_ASSERT_GREATER_THAN_UINT32(0u, ProjectConfig::kGreatScanStatusPollMs);
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_project_config_values);
  UNITY_END();
}

void loop() {}

#else
#include <cassert>

int main() {
  assert(ProjectConfig::kDeviceName != nullptr);
  assert(std::strlen(ProjectConfig::kDeviceName) > 0);

  assert(ProjectConfig::kStaSsid != nullptr);
  assert(std::strlen(ProjectConfig::kStaSsid) > 0);

  assert(ProjectConfig::kStaPassword != nullptr);
  assert(std::strlen(ProjectConfig::kStaPassword) >= 8);

  assert(ProjectConfig::kGreatScanStatusUrl != nullptr);
  assert(std::strstr(ProjectConfig::kGreatScanStatusUrl, "/api/status") != nullptr);

  assert(ProjectConfig::kGreatScanRawUrl != nullptr);
  assert(std::strstr(ProjectConfig::kGreatScanRawUrl, "/api/raw") != nullptr);

  assert(ProjectConfig::kGreatScanStatusPollMs > 0);

  return 0;
}
#endif
