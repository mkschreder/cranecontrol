#pragma once

#include <stdint.h>
#include <stddef.h>

uint16_t crc16(uint16_t crc, const uint8_t *buffer, size_t len);
