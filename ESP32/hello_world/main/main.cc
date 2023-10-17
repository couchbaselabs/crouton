#include "Crouton.hh"
#include "ESPAddrInfo.hh"
#include "ESPTCPSocket.hh"
#include <lwip/dns.h>

#include "sdkconfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_chip_info.h>
#include <esp_event.h>
#include <esp_flash.h>
#include <esp_pthread.h>
#include <nvs_flash.h>
#include <esp_netif.h>

using namespace std;
using namespace crouton;


void RunCoroutine(Future<void> (*test)()) {
    Future<void> f = test();
    Scheduler::current().runUntil([&]{return f.hasResult();});
    f.result(); // check exception
}


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


void coro_main() {
    printf("---------- CORO MAIN ----------\n\n");
    InitLogging();
//    LCoro->set_level(spdlog::level::trace);
//    LLoop->set_level(spdlog::level::trace);
//    LSched->set_level(spdlog::level::trace);
    LNet->set_level(spdlog::level::trace);

    RunCoroutine([]() -> Future<void> {

        spdlog::info("Testing Generator");
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

        spdlog::info("Testing AddrInfo -- looking up example.com");
        {
            esp::AddrInfo addr = AWAIT esp::AddrInfo::lookup("example.com");
            printf("Addr = %s\n", addr.primaryAddressString().c_str());
            auto ip4addr = addr.primaryAddress();
            postcondition(ip4addr.type == IPADDR_TYPE_V4);
            postcondition(addr.primaryAddressString() == "93.184.216.34");
        }

        spdlog::info("Testing TCPSocket");
        {
            esp::TCPSocket socket;
            AWAIT socket.connect("example.com", 80);

            spdlog::info("-- Connected! Test Writing...");
            AWAIT socket.write(string_view("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n"));

            spdlog::info("-- Test Reading...");
            string result = AWAIT socket.readAll();

            spdlog::info("Got HTTP response");
            printf("%s\n", result.c_str());
            postcondition(result.starts_with("HTTP/1.1 "));
            postcondition(result.size() > 1000);
            postcondition(result.size() < 2000);
        }
        
        spdlog::info("End of tests");
        RETURN noerror;
    });
    postcondition(Scheduler::current().assertEmpty());

    printf("\n---------- END CORO MAIN ----------\n");
}


extern "C" esp_err_t example_connect(void);

// Taken from ESP-IDF "hello world" example
extern "C"
void app_main(void)
{
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

    // Start a thread to run Crouton. Necessary because `std::this_thread` calls pthreads API,
    // which crashes if not called on a thread created by pthreads.
    std::thread t(coro_main);
    t.join();

    printf("Minimum heap space was %ld bytes\n", esp_get_minimum_free_heap_size());
    printf("Restarting in 10 seconds...");
    fflush(stdout);
    for (int i = 9; i >= 0; i--) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        printf(" %d ...", i);
        fflush(stdout);
    }
    printf(" Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
