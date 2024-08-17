#include <stdio.h>
#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_mac.h"

#include "nvs_flash.h"
#include "dht.h"
#include "driver/gpio.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "protocol_examples_common.h"

#define TAG "OTA"
#define PIN 2

extern const uint8_t server_cert_pem_start[] asm("_binary_google_cer_start"); //defining the path for the digital certificate (.cer)
static const uint8_t new_mac_address[6]= {0xf4, 0x96, 0x34, 0x9d, 0xe2, 0x11}; //setting up custom MAC address
SemaphoreHandle_t ota_semaphore;

/*Event handler for the client*/

esp_err_t client_event_handler(esp_http_client_event_t *evt)
{
  return ESP_OK;
}

/*Function for comparing the versions of the Firmware*/

esp_err_t validate_image_header(esp_app_desc_t *incoming_ota_desc)
{
  const esp_partition_t *running_partition = esp_ota_get_running_partition();
  esp_app_desc_t running_partition_description;
  esp_ota_get_partition_description(running_partition, &running_partition_description);

  ESP_LOGI(TAG, "Current version of Firmware is %s\n", running_partition_description.version);
  ESP_LOGI(TAG, "New version of Firmware is %s\n", incoming_ota_desc->version);

  if (strcmp(running_partition_description.version, incoming_ota_desc->version) == 0)
  {
    ESP_LOGW(TAG, "NEW VERSION IS THE SAME AS CURRENT VERSION. ABORTING");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "New Version Detected : OTA will be Performed !!");
  return ESP_OK;
}

void run_ota(void *params)
{
  /*Overwriting with custom MAC address set by the base MAC address*/
  ESP_ERROR_CHECK(esp_iface_mac_addr_set(new_mac_address, ESP_MAC_WIFI_STA)); 

  /*Initializing the NVS partition which is specified in the menuconfiguration*/
  ESP_ERROR_CHECK(nvs_flash_init());

  /*Handling network configuration, IP address assignment, DNS configuration and other network related task*/
  esp_netif_init();

  /*Managing events within application and other asynchronous tasks and inter module communication*/
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  while (true)
  {
    xSemaphoreTake(ota_semaphore, portMAX_DELAY); //task in blocking mode till the boot button is pressed and semaphore is released
    ESP_LOGI(TAG, "Invoking OTA");
    ESP_ERROR_CHECK(example_connect());//connecting to the wifi and configuring

/*defining structure members of client configuration*/

    esp_http_client_config_t clientConfig = {
        .url = "https://drive.google.com/u/0/uc?id=1V6dds0d64HQeP1QAKBW0A5pEiYEJnO2e&export=download", // our ota location
        .event_handler = client_event_handler, //calling the handler of the client and checking if everything is ok
        .cert_pem = (char *)server_cert_pem_start, //digital certificate of the the server
        .timeout_ms = 10000}; //timeout of handshake, if it is not executed properly

/*ESP HTTP client configuration by passing client configuration details as specified above*/

    esp_https_ota_config_t ota_config = {
        .http_config = &clientConfig};

    esp_https_ota_handle_t ota_handle = NULL; //handle for the OTA

/*Starting HTTPS OTA firmware upgrade and establishing HTTPS connection, passing configuration structure and OTA Handle*/

    if (esp_https_ota_begin(&ota_config, &ota_handle) != ESP_OK)
    {
      ESP_LOGE(TAG, "esp_https_ota_begin failed");
      example_disconnect();
      continue;
    }

    ESP_LOGI(TAG, "Beginning OTA Process !");

/*Reading the Firmware version and validating, this api is not mandatory but is used for error checking if version is proper or not*/

    esp_app_desc_t incoming_ota_desc;
    if (esp_https_ota_get_img_desc(ota_handle, &incoming_ota_desc) != ESP_OK)
    {
      ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed");
      esp_https_ota_finish(ota_handle);
      example_disconnect();
      continue;
    }

    ESP_LOGI(TAG, "Sucess in Reading OTA Image/Descriptor so Far");

/*Here the firmware version of the current binary is compared with the firmware version of the binary stored in the server*/

    if (validate_image_header(&incoming_ota_desc) != ESP_OK) //function calling and passing the address of the handler
    {
      ESP_LOGE(TAG, "validate_image_header failed");
      esp_https_ota_finish(ota_handle);
      example_disconnect();
      continue;
    }

/*If the fimware version is different, data is collected from the respected URL, written into the OTA partition*/

    while (true)
    {
      esp_err_t ota_result = esp_https_ota_perform(ota_handle);
      if (ota_result != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
        break;
    }

/*Cleaning up after successfully downloading new binary and switching the boot partition to the new OTA partition*/

    if (esp_https_ota_finish(ota_handle) != ESP_OK)
    {
      ESP_LOGE(TAG, "esp_https_ota_finish failed");
      example_disconnect();
      continue;
    }
    else
    {
      printf("Restarting in 5 seconds\n");
      vTaskDelay(pdMS_TO_TICKS(5000));
      esp_restart();
    }
    ESP_LOGE(TAG, "Failed to update firmware");
  }
}

/*Restarting the ESP after successfully flashing the new firmware*/

/*Defining the interrupt and releasing the semaphore*/

void on_button_pushed(void *params)
{
  xSemaphoreGiveFromISR(ota_semaphore, pdFALSE);
}

/*Main BEGIN*/

void app_main(void)
{
    printf("This version is with the Humidity and Temperature Sensor :) \n");

/*Getting the details of the verions using predefined functions (not necessary just for the information sake) */

  const esp_partition_t *running_partition = esp_ota_get_running_partition();
  esp_app_desc_t running_partition_description;
  esp_ota_get_partition_description(running_partition, &running_partition_description);
  printf("Current firmware version is: %s\n", running_partition_description.version);

  /*Configuring BOOT button as an interrupt*/

  gpio_config_t gpioConfig = {
      .pin_bit_mask = 1ULL << GPIO_NUM_0,
      .mode = GPIO_MODE_DEF_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLUP_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE};

  gpio_config(&gpioConfig);//configuring the gpio by passing the gpio config details
  gpio_install_isr_service(0);
  gpio_isr_handler_add(GPIO_NUM_0, on_button_pushed, NULL); //adding ISR handler to the GPIO PIN

/*Creating semaphore for the OTA function*/

  ota_semaphore = xSemaphoreCreateBinary();
    gpio_set_direction(PIN, GPIO_MODE_OUTPUT); //setting the PIN 2 as output
  xTaskCreate(run_ota, "run_ota", 1024 * 8, NULL, 2, NULL); //creating task for the OTA function

/*main loop which will be executing after the successfull updation (functionality depending upon the user)*/

    while(true)
    {
        int16_t humidity, temperarture;
        //printf("ESP32 DHT11 TEST: %s, %s \r \n", __DATE__, __TIME__);
        gpio_set_level(PIN, 1);
        dht_read_data(DHT_TYPE_DHT11, 17 , &humidity, &temperarture);
        vTaskDelay(1000/ portTICK_PERIOD_MS);
        gpio_set_level(PIN, 0);
        vTaskDelay(1000/ portTICK_PERIOD_MS);
        printf("Humidity and Temperature : %d %d\n", humidity/10, temperarture/10);
    }

}


