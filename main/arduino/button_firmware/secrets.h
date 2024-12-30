/**
 * @file    secret.h
 * @brief   Shared secret variables for rolling code generation
 * @details The variables are used for rolling code generation and extra obfusctaion during broadcasting
 */

#ifndef SECRETS_H
#define SECRETS_H

// Product configuration
#define PRODUCT_KEY 0x12345678UL
#define BATCH_ID 0x0001U
// Custom BLE manufacturer ID
#define MANUFACTURER_ID 0xF001U

#endif // SECRETS_H