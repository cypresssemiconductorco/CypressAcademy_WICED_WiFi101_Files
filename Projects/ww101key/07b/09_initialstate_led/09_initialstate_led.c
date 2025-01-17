/* Connect to Initial State and send LED ON/OFF commands */
#include <stdlib.h>
#include "wiced.h"
#include "wiced_tls.h"
#include "http_client.h"

#define SERVER_HOST        "groker.initialstate.com"
#define JSON_MSG1          "{\"key\":\"LED_State\", \"value\":\"ON\"}"
#define JSON_MSG2          "{\"key\":\"LED_State\", \"value\":\"OFF\"}"

#define SERVER_PORT        ( 443 )
#define DNS_TIMEOUT_MS     ( 10000 )
#define CONNECT_TIMEOUT_MS ( 3000 )

static void  event_handler( http_client_t* client, http_event_t event, http_response_t* response );
static void  print_data   ( char* data, uint32_t length );

static const char root_ca_certificate[] =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIEDzCCAvegAwIBAgIBADANBgkqhkiG9w0BAQUFADBoMQswCQYDVQQGEwJVUzEl\n"
        "MCMGA1UEChMcU3RhcmZpZWxkIFRlY2hub2xvZ2llcywgSW5jLjEyMDAGA1UECxMp\n"
        "U3RhcmZpZWxkIENsYXNzIDIgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMDQw\n"
        "NjI5MTczOTE2WhcNMzQwNjI5MTczOTE2WjBoMQswCQYDVQQGEwJVUzElMCMGA1UE\n"
        "ChMcU3RhcmZpZWxkIFRlY2hub2xvZ2llcywgSW5jLjEyMDAGA1UECxMpU3RhcmZp\n"
        "ZWxkIENsYXNzIDIgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwggEgMA0GCSqGSIb3\n"
        "DQEBAQUAA4IBDQAwggEIAoIBAQC3Msj+6XGmBIWtDBFk385N78gDGIc/oav7PKaf\n"
        "8MOh2tTYbitTkPskpD6E8J7oX+zlJ0T1KKY/e97gKvDIr1MvnsoFAZMej2YcOadN\n"
        "+lq2cwQlZut3f+dZxkqZJRRU6ybH838Z1TBwj6+wRir/resp7defqgSHo9T5iaU0\n"
        "X9tDkYI22WY8sbi5gv2cOj4QyDvvBmVmepsZGD3/cVE8MC5fvj13c7JdBmzDI1aa\n"
        "K4UmkhynArPkPw2vCHmCuDY96pzTNbO8acr1zJ3o/WSNF4Azbl5KXZnJHoe0nRrA\n"
        "1W4TNSNe35tfPe/W93bC6j67eA0cQmdrBNj41tpvi/JEoAGrAgEDo4HFMIHCMB0G\n"
        "A1UdDgQWBBS/X7fRzt0fhvRbVazc1xDCDqmI5zCBkgYDVR0jBIGKMIGHgBS/X7fR\n"
        "zt0fhvRbVazc1xDCDqmI56FspGowaDELMAkGA1UEBhMCVVMxJTAjBgNVBAoTHFN0\n"
        "YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4xMjAwBgNVBAsTKVN0YXJmaWVsZCBD\n"
        "bGFzcyAyIENlcnRpZmljYXRpb24gQXV0aG9yaXR5ggEAMAwGA1UdEwQFMAMBAf8w\n"
        "DQYJKoZIhvcNAQEFBQADggEBAAWdP4id0ckaVaGsafPzWdqbAYcaT1epoXkJKtv3\n"
        "L7IezMdeatiDh6GX70k1PncGQVhiv45YuApnP+yz3SFmH8lU+nLMPUxA2IGvd56D\n"
        "eruix/U0F47ZEUD0/CwqTRV/p2JdLiXTAAsgGh1o+Re49L2L7ShZ3U0WixeDyLJl\n"
        "xy16paq8U4Zt3VekyvggQQto8PT7dL5WXXp59fkdheMtlb71cZBDzI0fmgAKhynp\n"
        "VSJYACPq4xJDKVtHCN2MQWplBqjlIapBtJUhlbl90TSrE9atvNziPTnNvT51cKEY\n"
        "WQPJIrSPnNVeKtelttQKbfi3QBFGmh95DmK/D5fs4C8fF5Q=\n"
        "-----END CERTIFICATE-----\n";

static wiced_semaphore_t httpWait;
static wiced_semaphore_t buttonWait;

static http_client_t  client;
static http_request_t request;
static http_client_configuration_info_t client_configuration;
static char json_size[5]; // This holds the size of the JSON message as a decimal value
static char message[50];  // This holds the JSON message to be sent
http_header_field_t header[5]; // Array of headers

/******************************************************
 *               Function Definitions
 ******************************************************/
/* Interrupt service routine for the button */
void button_isr(void* arg)
{
    static wiced_bool_t flag;
    if (flag == WICED_TRUE)
    {
        flag = WICED_FALSE;
        sprintf(message,"%s",JSON_MSG1);            // set message
        sprintf(json_size,"%d",strlen(JSON_MSG1));  // set length of message
        WPRINT_APP_INFO( ( "Sending LED ON\n") );
    }
    else
    {
        flag = WICED_TRUE;
        sprintf(message,"%s",JSON_MSG2);            // set message
        sprintf(json_size,"%d",strlen(JSON_MSG2));  // set length of message
        WPRINT_APP_INFO( ( "Sending LED OFF\n") );
    }

    wiced_rtos_set_semaphore(&buttonWait);
}

void application_start( void )
{
    wiced_ip_address_t  ip_address;
    wiced_result_t      result;

    /* Header 0 is the Host header */
    header[0].field        = HTTP_HEADER_HOST;
    header[0].field_length = strlen( HTTP_HEADER_HOST );
    header[0].value        = SERVER_HOST;
    header[0].value_length = strlen( SERVER_HOST );

    /* Header 1 is the BucketKey */
    header[1].field        =  "X-IS-BucketKey: ";
    header[1].field_length = strlen( "X-IS-BucketKey: " );
    header[1].value        = "KDS3L8DG59VP";
    header[1].value_length = strlen( "KDS3L8DG59VP" );

    /* Header 2 is the AccessKey */
    header[2].field        =  "X-IS-AccessKey: ";
    header[2].field_length = strlen( "X-IS-AccessKey: " );
    header[2].value        = "kyb54q2ocuspmjrPEa3k6MqNlkWaYUo2";
    header[2].value_length = strlen( "kyb54q2ocuspmjrPEa3k6MqNlkWaYUo2" );

    /* Header 3 is the content type (JSON) */
    header[3].field        =  HTTP_HEADER_CONTENT_TYPE;
    header[3].field_length = strlen( HTTP_HEADER_CONTENT_TYPE );
    header[3].value        = "application/json";
    header[3].value_length = strlen( "application/json" );

    /* Header 4 is the application content length. This will be set when we need it later since it changes. */

    wiced_init( );

    wiced_rtos_init_semaphore(&httpWait);
    wiced_rtos_init_semaphore(&buttonWait);

    wiced_network_up(WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL);

    WPRINT_APP_INFO( ( "Resolving IP address of %s\n", SERVER_HOST ) );
    wiced_hostname_lookup( SERVER_HOST, &ip_address, DNS_TIMEOUT_MS, WICED_STA_INTERFACE );
    WPRINT_APP_INFO( ( "%s is at %u.%u.%u.%u\n", SERVER_HOST,
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 24),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 16),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 8),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 0) ) );

    /* Initialize the root CA certificate */
    result = wiced_tls_init_root_ca_certificates( root_ca_certificate, strlen(root_ca_certificate) );
    if ( result != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "Error: Root CA certificate failed to initialize: %u\n", result) );
        return;
    }

    http_client_init( &client, WICED_STA_INTERFACE, event_handler, NULL );

    /* configure HTTP client parameters */
    client_configuration.flag = (http_client_configuration_flags_t)(HTTP_CLIENT_CONFIG_FLAG_SERVER_NAME | HTTP_CLIENT_CONFIG_FLAG_MAX_FRAGMENT_LEN);
    client_configuration.server_name = (uint8_t*) SERVER_HOST;
    client_configuration.max_fragment_length = TLS_FRAGMENT_LENGTH_1024;
    http_client_configure(&client, &client_configuration);

    /* If you set client.peer_cn to the name of the host you are trying to connect to, the library will make
     * sure it matches the certificate sent by the server */
    client.peer_cn = (uint8_t*) SERVER_HOST;

    wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, button_isr, NULL); /* Setup interrupt */

    WPRINT_APP_INFO( ( "Press WICED_BUTTON1 to Toggle LED State\n" ) );

    while(1)
    {
        /* Wait for a button press */
        wiced_rtos_get_semaphore(&buttonWait, WICED_WAIT_FOREVER);

        /* Connect to the server */
        WPRINT_APP_INFO( ( "Connecting to %s\n", SERVER_HOST ) );
        if ( ( result = http_client_connect( &client, (const wiced_ip_address_t*)&ip_address, SERVER_PORT, HTTP_USE_TLS, CONNECT_TIMEOUT_MS ) ) != WICED_SUCCESS )
        {
            WPRINT_APP_INFO( ( "Error: failed to connect to server: %u\n", result) );
            return;
        }
        WPRINT_APP_INFO( ( "Connected\n" ) );

        /* Set header for length of JSON message being sent */
        header[4].field        =  HTTP_HEADER_CONTENT_LENGTH;
        header[4].field_length = strlen( HTTP_HEADER_CONTENT_LENGTH );
        header[4].value        = json_size;
        header[4].value_length = strlen( json_size );

        /* Setup the POST request */
        http_request_init( &request, &client, HTTP_POST, "/api/events", HTTP_1_1 );
        http_request_write_header( &request, &header[0], 5 );
        http_request_write_end_header( &request );
        http_request_write( &request, (uint8_t*) message, strlen(message));
        http_request_flush( &request );

        /* Wait for request to complete */
        wiced_rtos_get_semaphore(&httpWait, WICED_WAIT_FOREVER);

        /* Close connection */
        http_client_disconnect(&client);
    }
}

/* This is the callback event for HTTP events */
static void event_handler( http_client_t* client, http_event_t event, http_response_t* response )
{
    switch( event )
    {
        case HTTP_CONNECTED:
            /* This state is never called */
            break;

        case HTTP_DISCONNECTED:
        {
            http_client_disconnect( client ); /* Need to keep client connection state synchronized with the server */
            WPRINT_APP_INFO(( "Disconnected from %s\n", SERVER_HOST ));
            break;
        }
        case HTTP_DATA_RECEIVED:
        {
            WPRINT_APP_INFO( ( "------------------ Received response ------------------\n" ) );

            /* Print Response Header */
            if(response->response_hdr != NULL)
            {
                WPRINT_APP_INFO( ( "----- Response Header: -----\n " ) );
                print_data( (char*) response->response_hdr, response->response_hdr_length );
            }

            /* Print Response Payload  */
            WPRINT_APP_INFO( ("\n----- Response Payload: -----\n" ) );
            print_data( (char*) response->payload, response->payload_data_length );

            if(response->remaining_length == 0)
            {
               WPRINT_APP_INFO( ("\n------------------ End Response ------------------\n" ) );
               http_request_deinit( (http_request_t*) &(response->request) );
               wiced_rtos_set_semaphore(&httpWait); // Set semaphore to flag that this request is done
            }
            break;
        }
        default:
        break;
    }
}

/* Helper function to print data in the response to the UART */
static void print_data( char* data, uint32_t length )
{
    uint32_t a;

    for ( a = 0; a < length; a++ )
    {
        WPRINT_APP_INFO( ( "%c", data[a] ) );
    }
}

