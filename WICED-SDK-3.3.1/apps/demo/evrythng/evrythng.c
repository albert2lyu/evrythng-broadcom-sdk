/*
 * Copyright 2014, Broadcom Corporation
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 */

#include "wiced.h"
#include "JSON.h"

#include "evrythng.h"
#include "evrythng_platform.h"

/******************************************************
 *                      Macros
 ******************************************************/

#define MQTT_URL "tcp://mqtt.evrythng.com:1883"

#define DEVICE_API_KEY "iaaRIcy8WGvDSzgA4xX6IUac4jsjnvAtmMunXw0cigtlbxCvgIsJii6LrhEfPJifxeyRvvjyiqdmWXNg"

#define EVRYTHNG_BROADCOM_THNG "UC5tXtqQPVpRPHKUHs7Ggkrr"

#define BUTTON_1_PROPERTY "button_1"
#define BUTTON_2_PROPERTY "button_2"
#define RED_LED_PROPERTY   "red_led"
#define GREEN_LED_PROPERTY "green_led"


/******************************************************
 *                    Constants
 ******************************************************/

/******************************************************
 *                   Enumerations
 ******************************************************/

/******************************************************
 *                 Type Definitions
 ******************************************************/

/******************************************************
 *                    Structures
 ******************************************************/

/******************************************************
 *               Static Function Declarations
 ******************************************************/

static void log_callback(evrythng_log_level_t level, const char* fmt, va_list vl);
static void conlost_callback(evrythng_handle_t h);
static void property_callback(const char* str_json, size_t len);

static wiced_result_t json_callback(wiced_json_object_t* json_object);

static void evrythng_sub_thread(uint32_t arg);

/******************************************************
 *               Variable Definitions
 ******************************************************/

/******************************************************
 *                 Global Variables
 ******************************************************/

evrythng_handle_t evt_handle;

/******************************************************
 *               Function Declarations
 ******************************************************/

void application_start(void)
{
    /* Initialise the device and WICED framework */
    wiced_init( );

    /* Bring up the network interface */
    wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );

    evrythng_init_handle(&evt_handle);
    evrythng_set_log_callback(evt_handle, log_callback);
    evrythng_set_conlost_callback(evt_handle, conlost_callback);
    evrythng_set_url(evt_handle, MQTT_URL);
    evrythng_set_key(evt_handle, DEVICE_API_KEY);

    wiced_JSON_parser_register_callback(json_callback);

    platform_printf("Connecting to %s\n", MQTT_URL);
    while (evrythng_connect(evt_handle) != EVRYTHNG_SUCCESS) 
    {
        platform_printf("Retrying\n");
        wiced_rtos_delay_milliseconds(2000);
    }
    platform_printf("Evrythng client Connected\n");

    wiced_thread_t sub_thread;
    if (wiced_rtos_create_thread(&sub_thread, 3, "evrythng_subscriptions", evrythng_sub_thread, 4096, evt_handle) != WICED_SUCCESS)
    {
        platform_printf("failed to create subscription thread\n");
    }

    char msg[128];

    wiced_bool_t previous_button1_state;
    wiced_bool_t previous_button2_state;
    wiced_bool_t button1_pressed;
    wiced_bool_t button2_pressed;

    previous_button1_state = button1_pressed = WICED_FALSE;
    previous_button2_state = button2_pressed = WICED_FALSE;

    while (1)
    {
        /* Read the state of Button 1 */
        button1_pressed = wiced_gpio_input_get( WICED_BUTTON1 ) ? WICED_FALSE : WICED_TRUE;  /* The button has inverse logic */

        if (button1_pressed != previous_button1_state)
        {
            button1_pressed == WICED_TRUE ? sprintf(msg, "[{\"value\": true}]") : sprintf(msg, "[{\"value\": false}]");
            previous_button1_state = button1_pressed;
            evrythng_publish_thng_property(evt_handle, EVRYTHNG_BROADCOM_THNG, BUTTON_1_PROPERTY, msg, 0);
        }

        /* Read the state of Button 2 */
        button2_pressed = wiced_gpio_input_get( WICED_BUTTON2 ) ? WICED_FALSE : WICED_TRUE;  /* The button has inverse logic */

        if (button2_pressed != previous_button2_state)
        {
            button2_pressed == WICED_TRUE ? sprintf(msg, "[{\"value\": true}]") : sprintf(msg, "[{\"value\": false}]");
            previous_button2_state = button2_pressed;
            evrythng_publish_thng_property(evt_handle, EVRYTHNG_BROADCOM_THNG, BUTTON_2_PROPERTY, msg, 0);
        }

        wiced_rtos_delay_milliseconds( 250 );
    }
}


void evrythng_sub_thread(uint32_t arg)
{
    evrythng_handle_t evt_handle = (evrythng_handle_t)arg;

    platform_printf("Subscribing to property %s\n", RED_LED_PROPERTY);
    evrythng_subscribe_thng_property(evt_handle, EVRYTHNG_BROADCOM_THNG, RED_LED_PROPERTY, property_callback);
    platform_printf("Subscribing to property %s\n", GREEN_LED_PROPERTY);
    evrythng_subscribe_thng_property(evt_handle, EVRYTHNG_BROADCOM_THNG, GREEN_LED_PROPERTY, property_callback);

    evrythng_message_loop(evt_handle);
}

void log_callback(evrythng_log_level_t level, const char* fmt, va_list vl)
{
    char msg[512];

    unsigned n = vsnprintf(msg, sizeof msg, fmt, vl);
    if (n >= sizeof msg)
        msg[sizeof msg - 1] = '\0';

    switch (level)
    {
        case EVRYTHNG_LOG_ERROR:
            platform_printf("ERROR: ");
            break;
        case EVRYTHNG_LOG_WARNING:
            platform_printf("WARNING: ");
            break;
        default:
        case EVRYTHNG_LOG_DEBUG:
            platform_printf("DEBUG: ");
            break;
    }
    platform_printf("%s\n", msg);
}

void conlost_callback(evrythng_handle_t h)
{
    platform_printf("connection lost, trying to reconnect\n");
    while (evrythng_connect(h) != EVRYTHNG_SUCCESS) {
        platform_printf("Retrying\n");
        wiced_rtos_delay_milliseconds(2000);
    }
}

static int led_to_swtich = -1;
static int led_value = -1;

void property_callback(const char* str_json, size_t len)
{
    char msg[len+1]; snprintf(msg, sizeof msg, "%s", str_json);
    platform_printf("Received message: %s\n", msg);

    wiced_JSON_parser(str_json, len);

    if (led_value >= 0 && led_to_swtich >= 0)
    {
        if (led_value) wiced_gpio_output_high(led_to_swtich);
        else wiced_gpio_output_low(led_to_swtich);
    }

    led_to_swtich = -1;
    led_value = -1;
}

wiced_result_t json_callback(wiced_json_object_t* json_object)
{
    if (!json_object)
        return WICED_SUCCESS;

    switch(json_object->value_type)
    {
        case JSON_STRING_TYPE:
            if (!strncmp(json_object->object_string, "key", json_object->object_string_length))
            {
                if (!strncmp(json_object->value, RED_LED_PROPERTY, json_object->value_length))
                    led_to_swtich = WICED_LED1;
                else if (!strncmp(json_object->value, GREEN_LED_PROPERTY, json_object->value_length))
                    led_to_swtich = WICED_LED2;
            }
            break;
        case JSON_BOOLEAN_TYPE:
            if (!strncmp(json_object->object_string, "value", json_object->object_string_length))
            {
                if (!strncmp(json_object->value, "false", json_object->value_length))
                    led_value = 0;
                else if (!strncmp(json_object->value, "true", json_object->value_length))
                    led_value = 1;
            }
            break;
        default:
            break;
    }
    
    return WICED_SUCCESS;
}