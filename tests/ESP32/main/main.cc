#include "Crouton.hh"
#include "util/Logging.hh"

#include "sdkconfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_chip_info.h>
#include <esp_event.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_pthread.h>
#include <lwip/dns.h>
#include <nvs_flash.h>

#include <thread>

static void initialize();

using namespace std;
using namespace crouton;


static Generator<int64_t> fibonacci(int64_t limit, bool slow = false) {
    int64_t a = 1, b = 1;
    YIELD a;
    while (b <= limit) {
        YIELD b;
        tie(a, b) = pair{b, a + b};
        if (slow)
            AWAIT Timer::sleep(0.1);
    }
}


Task mainTask() {
    initialize();

    printf("---------- TESTING CROUTON ----------\n\n");
    esp_log_level_set("Crouton", ESP_LOG_DEBUG);
//    LCoro->set_level(LogLevel::trace);
//    LLoop->set_level(LogLevel::trace);
//    LSched->set_level(LogLevel::trace);
    LNet->set_level(LogLevel::trace);

    Log->info("Testing Generator");
    {
        Generator<int64_t> fib = fibonacci(100, true);
        vector<int64_t> results;
        Result<int64_t> result;
        while ((result = AWAIT fib)) {
            printf("%lld ", result.value());
            results.push_back(result.value());
        }
        printf("\n");
    }

    Log->info("Testing AddrInfo -- looking up example.com");
    {
        io::AddrInfo addr = AWAIT io::AddrInfo::lookup("example.com");
        printf("Addr = %s\n", addr.primaryAddressString().c_str());
        auto ip4addr = addr.primaryAddress();
        postcondition(ip4addr.type == IPADDR_TYPE_V4);
        postcondition(addr.primaryAddressString() == "93.184.216.34");
    }

    Log->info("Testing TCPSocket with TLS");
    {
        auto socket = io::ISocket::newSocket(true);
        AWAIT socket->connect("example.com", 443);

        Log->info("-- Connected! Test Writing...");
        AWAIT socket->stream().write(string_view("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n"));

        Log->info("-- Test Reading...");
        string result = AWAIT socket->stream().readAll();

        Log->info("Got HTTP response");
        printf("%s\n", result.c_str());
        postcondition(result.starts_with("HTTP/1.1 "));
        postcondition(result.size() > 1000);
        postcondition(result.size() < 2000);
    }

    Log->info("End of tests");
    postcondition(Scheduler::current().assertEmpty());

    printf("\n---------- END CROUTON TESTS ----------\n");

    
    printf("Minimum heap space was %ld bytes\n", esp_get_minimum_free_heap_size());
    printf("Restarting in 100 seconds...");
    fflush(stdout);
    for (int i = 99; i >= 0; i--) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        printf(" %d ...", i);
        fflush(stdout);
    }
    RETURN;
}

CROUTON_MAIN(mainTask)


// entry point of the `protocol_examples_common` component
extern "C" esp_err_t example_connect(void);

// Adapted from ESP-IDF "hello world" example
static void initialize() {
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }
    printf("%luMB %s flash\n", flash_size / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    printf("Heap space: %ld bytes ... internal %ld bytes\n",
           esp_get_free_heap_size(), esp_get_free_internal_heap_size());

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
}


