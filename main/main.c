#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "pico/stdlib.h"
#include <stdio.h>
#include "hardware/adc.h"
#include "hardware/uart.h"

typedef struct adc {
    int axis;
    int val;
} adc_t;

#define UART_ID uart0
#define BAUD_RATE 115200

#define UART_TX_PIN 0
#define UART_RX_PIN 1

const int BTN_PRIMARY_FIRE = 2;
const int BTN_SECONDARY_FIRE = 3;
const int BTN_INTERACT = 4;
const int BTN_JUMP = 5;

QueueHandle_t xMovementQueue;
QueueHandle_t xInputQueue;
QueueHandle_t xActionQueue;

void btn_callback(uint gpio, uint32_t events) {
    if (gpio == BTN_PRIMARY_FIRE && events == 0x4) { // fall edge
        xQueueSend(xInputQueue, &gpio, 0);
    } else if (gpio == BTN_SECONDARY_FIRE && events == 0x4) {
        xQueueSend(xInputQueue, &gpio, 0);
    } else if (gpio == BTN_INTERACT && events == 0x4) {
        xQueueSend(xInputQueue, &gpio, 0);
    } else if (gpio == BTN_JUMP && events == 0x4) {
        xQueueSend(xInputQueue, &gpio, 0);
    }
}

void process_input_task(void *p) {
    gpio_init(BTN_PRIMARY_FIRE);
    gpio_init(BTN_SECONDARY_FIRE);
    gpio_init(BTN_INTERACT);
    gpio_init(BTN_JUMP);

    gpio_set_dir(BTN_PRIMARY_FIRE, GPIO_IN);
    gpio_set_dir(BTN_SECONDARY_FIRE, GPIO_IN);
    gpio_set_dir(BTN_INTERACT, GPIO_IN);
    gpio_set_dir(BTN_JUMP, GPIO_IN);

    gpio_pull_up(BTN_PRIMARY_FIRE);
    gpio_pull_up(BTN_SECONDARY_FIRE);
    gpio_pull_up(BTN_INTERACT);
    gpio_pull_up(BTN_JUMP);

    gpio_set_irq_enabled_with_callback(BTN_PRIMARY_FIRE, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled_with_callback(BTN_SECONDARY_FIRE, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled_with_callback(BTN_INTERACT, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled_with_callback(BTN_JUMP, GPIO_IRQ_EDGE_FALL, true, &btn_callback);

    while (1) {
        int btn;
        if (xQueueReceive(xInputQueue, &btn, 1e6)) {
            switch (btn) {
                case BTN_PRIMARY_FIRE:
                    xQueueSend(xActionQueue, &btn, 0);
                    break;
                case BTN_SECONDARY_FIRE:
                    xQueueSend(xActionQueue, &btn, 0);
                    break;
                case BTN_INTERACT:
                    xQueueSend(xActionQueue, &btn, 0);
                    break;
                case BTN_JUMP:
                    xQueueSend(xActionQueue, &btn, 0);
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void x_task(void *p) {
    adc_init();
    adc_gpio_init(26);

    int vec[5] = {0, 0, 0, 0, 0};
    while (1) {
        adc_select_input(0);
        int result = adc_read();
        result = (result - 2048) / 14;

        vec[0] = vec[1];
        vec[1] = vec[2];
        vec[2] = vec[3];
        vec[3] = vec[4];
        vec[4] = result;
        result = (vec[4] + vec[3] + vec[2] + vec[1] + vec[0]) / 5;

        if (result >= 30 || result <= -30) {
            adc_t adc_x_data;
            adc_x_data.axis = 0;
            adc_x_data.val = result;

            xQueueSend(xMovementQueue, &adc_x_data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void y_task(void *p) {
    adc_init();
    adc_gpio_init(27);

    int vec[5] = {0, 0, 0, 0, 0};
    while (1) {
        adc_select_input(1);
        int result = adc_read();
        result = (result - 2048) / 14;

        vec[0] = vec[1];
        vec[1] = vec[2];
        vec[2] = vec[3];
        vec[3] = vec[4];
        vec[4] = result;
        result = (vec[4] + vec[3] + vec[2] + vec[1] + vec[0]) / 5;

        if (result >= 30 || result <= -30) {
            adc_t adc_y_data;
            adc_y_data.axis = 1;
            adc_y_data.val = result;

            xQueueSend(xMovementQueue, &adc_y_data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void uart_task(void *p) {
    adc_t data;
    while (true) {
        if (xQueueReceive(xMovementQueue, &data, 1e6)) {
            // printf("Eixo: %d, Valor: %d\n", data.axis, data.val);
            uint8_t axis = (uint8_t)data.axis;
            uint16_t val = (uint16_t)(data.val & 0xFFFF);
            uint8_t lsb = val & 0xFF;
            uint8_t msb = (val >> 8) & 0xFF;
            uint8_t end = 0xFF;

            uint8_t pacote[4] = {axis, lsb, msb, end};
            uart_write_blocking(UART_ID, pacote, 4);
        }
        if (xQueueReceive(xActionQueue, &data, 1e6)) {
            // printf("Eixo: %d, Valor: %d\n", data.axis, data.val);
            uint8_t axis = (uint8_t)data.axis;
            uint16_t val = (uint16_t)(data.val & 0xFFFF);
            uint8_t lsb = val & 0xFF;
            uint8_t msb = (val >> 8) & 0xFF;
            uint8_t end = 0xFF;

            uint8_t pacote[4] = {axis, lsb, msb, end};
            uart_write_blocking(UART_ID, pacote, 4);
        }
    }
}

int main() {
    stdio_init_all();
    uart_init(uart0, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    xInputQueue = xQueueCreate(32, sizeof(int));
    xActionQueue = xQueueCreate(32, sizeof(int));
    xMovementQueue = xQueueCreate(32, sizeof(adc_t));

    if (xInputQueue == NULL || xMovementQueue)
        printf("falha em criar a fila \n");

    xTaskCreate(process_input_task, "Process Input Task", 256, NULL, 1, NULL);
    xTaskCreate(x_task, "ADC X Task", 4095, NULL, 1, NULL);
    xTaskCreate(y_task, "ADC Y Task", 4095, NULL, 1, NULL);
    xTaskCreate(uart_task, "UART Task", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
