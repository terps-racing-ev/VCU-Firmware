#include "unity.h"
#include "data/config.h"
#include "Mockeeprom.h"

void setUp(void) {
    /* The generated runner initializes the mock before each test. */
}

void tearDown(void) {
    /* The generated runner verifies and destroys the mock after each test. */
}

void test_write_catches_over_value(void) {

    TEST_ASSERT_EQUAL(
        EEPROM_INVALID_ARGUMENT,
        Config_WriteVariable(CONFIG_CTRL_CAN_MANAGER_DELAY_MS, 300)
    );

}

void test_read_uses_default(void) {

    uint16_t value = 999;

    EEPROM_ReadVariable_ExpectAndReturn(
        CONFIG_DEFAULT_ADDR, &value, EEPROM_NOT_FOUND
    );

    TEST_ASSERT_EQUAL(
        EEPROM_OK,
        Config_ReadVariable(CONFIG_DEFAULT_ADDR, &value)
    );
    TEST_ASSERT_EQUAL_UINT16(CONFIG_DEFAULT_VALUE, value);

}
