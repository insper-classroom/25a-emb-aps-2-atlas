#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "pico/stdlib.h"
#include <stdio.h>
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/i2c.h"
#include "mpu6050.h"
#include "Fusion.h"
#include "hc06.h"

typedef struct adc {
    int axis;
    int val;
} adc_t;

typedef struct pos {
    int axis;
    float val;
} pos_t;

#define UART_ID uart0
#define SAMPLE_PERIOD (0.01f) // replace this with actual sample period
#define BAUD_RATE 115200

#define UART_TX_PIN 0
#define UART_RX_PIN 1

const int BTN_PRIMARY_FIRE = 10;
const int BTN_SECONDARY_FIRE = 11;
const int BTN_INTERACT = 12;
const int BTN_JUMP = 13;
const int MPU_ADDRESS = 0x68;
const int I2C_SDA_GPIO = 8;
const int I2C_SCL_GPIO = 9;

QueueHandle_t xMovementQueue;
QueueHandle_t xAimQueue;
QueueHandle_t xInputQueue;
QueueHandle_t xActionQueue;

void btn_callback(uint gpio, uint32_t events) {
    if (gpio == BTN_PRIMARY_FIRE && events == 0x4) { // fall edge
        xQueueSendFromISR(xInputQueue, &gpio, 0);
    } else if (gpio == BTN_SECONDARY_FIRE && events == 0x4) {
        xQueueSendFromISR(xInputQueue, &gpio, 0);
    } else if (gpio == BTN_INTERACT && events == 0x4) {
        xQueueSendFromISR(xInputQueue, &gpio, 0);
    } else if (gpio == BTN_JUMP && events == 0x4) {
        xQueueSendFromISR(xInputQueue, &gpio, 0);
    }
}

void send_actions(int val) {
    uint8_t action_byte = (uint8_t)val;
    uint8_t end = 0xFF;

    // Novo pacote com 7 bytes incluindo a ação
    uint8_t pacote[2] = {action_byte, end};
    uart_write_blocking(HC06_UART_ID, pacote, sizeof(pacote));
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
        int val = 0;
        if (xQueueReceive(xInputQueue, &btn, 1e6)) {
            switch (btn) {
            case BTN_PRIMARY_FIRE:
                val = 1;
                break;
            case BTN_SECONDARY_FIRE:
                val = 2;
                break;
            case BTN_INTERACT:
                val = 3;
                break;
            case BTN_JUMP:
                val = 4;
                break;
            }
        }

        xQueueSend(xActionQueue, &val, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void x_task(void *p) {
    adc_init();
    adc_gpio_init(26);

    while (1) {
        adc_select_input(0);
        int result = adc_read();
        result = (result - 2048) / 14;

        if (result < 30 && result > -30) {
            adc_t adc_x_data;
            adc_x_data.axis = 0;
            adc_x_data.val = 0;

            xQueueSend(xMovementQueue, &adc_x_data, 0);
        } else if (result >= 30) {
            adc_t adc_x_data;
            adc_x_data.axis = 0;
            adc_x_data.val = 1;

            xQueueSend(xMovementQueue, &adc_x_data, 0);
        } else if (result <= -30) {
            adc_t adc_x_data;
            adc_x_data.axis = 0;
            adc_x_data.val = 2;

            xQueueSend(xMovementQueue, &adc_x_data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void y_task(void *p) {
    adc_init();
    adc_gpio_init(27);

    while (1) {
        adc_select_input(1);
        int result = adc_read();
        result = (result - 2048) / 14;

        if (result < 30 && result > -30) {
            adc_t adc_y_data;
            adc_y_data.axis = 1;
            adc_y_data.val = 0;

            xQueueSend(xMovementQueue, &adc_y_data, 0);
        } else if (result >= 30) {
            adc_t adc_y_data;
            adc_y_data.axis = 1;
            adc_y_data.val = 1;

            xQueueSend(xMovementQueue, &adc_y_data, 0);
        } else if (result <= -30) {
            adc_t adc_y_data;
            adc_y_data.axis = 1;
            adc_y_data.val = 2;

            xQueueSend(xMovementQueue, &adc_y_data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void mpu6050_reset() {
    // Two byte reset. First byte register, second byte data
    // There are a load more options to set up the device in different ways that could be added here
    uint8_t buf[] = {0x6B, 0x00};
    i2c_write_blocking(i2c_default, MPU_ADDRESS, buf, 2, false);
}

static void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp) {
    // For this particular device, we send the device the register we want to read
    // first, then subsequently read from the device. The register is auto incrementing
    // so we don't need to keep sending the register we want, just the first.

    uint8_t buffer[6];

    // Start reading acceleration registers from register 0x3B for 6 bytes
    uint8_t val = 0x3B;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true); // true to keep master control of bus
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 6, false);

    for (int i = 0; i < 3; i++) {
        accel[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }

    // Now gyro data from reg 0x43 for 6 bytes
    // The register is auto incrementing on each read
    val = 0x43;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 6, false); // False - finished with bus

    for (int i = 0; i < 3; i++) {
        gyro[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
        ;
    }

    // Now temperature from reg 0x41 for 2 bytes
    // The register is auto incrementing on each read
    val = 0x41;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 2, false); // False - finished with bus

    *temp = buffer[0] << 8 | buffer[1];
}

void mpu6050_task(void *p) {
    // configuracao do I2C
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(I2C_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_GPIO);
    gpio_pull_up(I2C_SCL_GPIO);

    mpu6050_reset();
    int16_t acceleration[3], gyro[3], temp;

    FusionAhrs ahrs;
    FusionAhrsInitialise(&ahrs);

    while (1) {
        pos_t pos_data;

        // leitura da MPU, com fusão de dados
        mpu6050_read_raw(acceleration, gyro, &temp);
        FusionVector gyroscope = {
            .axis.x = gyro[0] / 131.0f, // Conversão para graus/s
            .axis.y = gyro[1] / 131.0f,
            .axis.z = gyro[2] / 131.0f,
        };

        FusionVector accelerometer = {
            .axis.x = acceleration[0] / 16384.0f, // Conversão para g
            .axis.y = acceleration[1] / 16384.0f,
            .axis.z = acceleration[2] / 16384.0f,
        };

        FusionAhrsUpdateNoMagnetometer(&ahrs, gyroscope, accelerometer, SAMPLE_PERIOD);

        const FusionEuler euler = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&ahrs));

        // printf("Acc. X = %d, Y = %d, Z = %d\n", acceleration[0], acceleration[1], acceleration[2]);
        // printf("Roll %0.1f, Pitch %0.1f, Yaw %0.1f\n", euler.angle.roll, euler.angle.pitch, euler.angle.yaw);
        // printf("Temp. = %f\n", (temp / 340.0) + 36.53);

        if (euler.angle.pitch != 0) {
            pos_data.axis = 0;
            pos_data.val = euler.angle.pitch;
            xQueueSend(xAimQueue, &pos_data, 0);
        }

        if (euler.angle.roll != 0) {
            pos_data.axis = 1;
            pos_data.val = euler.angle.roll;
            xQueueSend(xAimQueue, &pos_data, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void hc06_task(void *p) {
    uart_init(HC06_UART_ID, HC06_BAUD_RATE);
    gpio_set_function(HC06_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(HC06_RX_PIN, GPIO_FUNC_UART);
    hc06_init("P-Body", "1234");

    printf("HC-06 ready\n");

    adc_t data_movement;
    pos_t data_aim;
    int action = 0;

    while (true) {
        if (xQueueReceive(xAimQueue, &data_aim, 1e6) &&
            xQueueReceive(xMovementQueue, &data_movement, 1e6)) {

            if (xQueueReceive(xActionQueue, &action, 0) != pdTRUE) {
                action = 0;
            }

            uint8_t axis_aim = (uint8_t)data_aim.axis;
            int32_t val_aim = -1 * (int32_t)data_aim.val & 0xFFFF;
            uint8_t lsb = val_aim & 0xFF;
            uint8_t msb = (val_aim >> 8) & 0xFF;

            uint8_t axis_movement = (uint8_t)data_movement.axis;
            uint8_t val_movement = (uint8_t)data_movement.val;

            uint8_t action_byte = (uint8_t)action;
            uint8_t end = 0xFF;

            // Novo pacote com 7 bytes incluindo a ação
            uint8_t pacote[7] = {axis_aim, lsb, msb, axis_movement, val_movement, action_byte, end};
            uart_write_blocking(HC06_UART_ID, pacote, sizeof(pacote));
        }

        // Funcionam:
        // uint8_t pacote[7] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
        // uart_write_blocking(HC06_UART_ID, pacote, sizeof(pacote));
        // uart_puts(HC06_UART_ID, "Hello from HC-06!\r\n");
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
    xAimQueue = xQueueCreate(32, sizeof(pos_t));

    if (xInputQueue == NULL || xActionQueue == NULL || xMovementQueue == NULL || xAimQueue == NULL)
        printf("falha em criar a fila \n");

    xTaskCreate(process_input_task, "Process Input Task", 256, NULL, 1, NULL);
    xTaskCreate(x_task, "ADC X Task", 4095, NULL, 1, NULL);
    xTaskCreate(y_task, "ADC Y Task", 4095, NULL, 1, NULL);
    xTaskCreate(mpu6050_task, "mpu6050 Task 1", 8192, NULL, 1, NULL);
    xTaskCreate(hc06_task, "hc06 Task", 4096, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
