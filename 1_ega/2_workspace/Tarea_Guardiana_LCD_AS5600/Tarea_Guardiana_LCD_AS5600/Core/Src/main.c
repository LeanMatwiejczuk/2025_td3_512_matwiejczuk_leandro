/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lcd_i2c.h"
#include <stdio.h>
#include <string.h>
#include "semphr.h"
#include "math.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define LEFT 0
#define RIGHT 1
#define STOP 2

#define PIN_PWM GPIO_PIN_9
#define PORT_PWM GPIOA

#define PIN_IN1 GPIO_PIN_10
#define PORT_IN1 GPIOA

#define PIN_IN2 GPIO_PIN_11
#define PORT_IN2 GPIOA

#define PID_RUN 1
#define PID_STOP 0

#define INC_STEP 1.0f   // incremento al presionar subir/bajar (grados)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c2;

TIM_HandleTypeDef htim1;

/* USER CODE BEGIN PV */


QueueHandle_t I2C_txQueue;
QueueHandle_t I2C_rxQueue;
QueueHandle_t angleQueue;
QueueHandle_t goalangleQueue; // cola para setpoint (PID)
QueueHandle_t rtcQueue;

SemaphoreHandle_t goalangleSemphr;  // controla bloqueo entre menú <-> PID
SemaphoreHandle_t sem_btn0;
SemaphoreHandle_t sem_btn1;
SemaphoreHandle_t sem_btn2;
SemaphoreHandle_t sem_btn3;

typedef enum { RECEIVE, TRANSMIT } i2c_op_t;
typedef struct {
    uint16_t device_id;
    uint8_t reg;
    i2c_op_t type;
    uint8_t n_bytes;
    uint8_t *data;
} i2cQueue_t;

typedef struct {
    uint8_t sec, min, hour, day, month, year;
} rtc_time_t;

typedef enum {
    MENU_SETPOINT = 0,
    MENU_SET_TIME,
    MENU_MARGEN,
    MENU_DATALOGGER,
    MENU_TOTAL_ITEMS
} menu_option_t;

/* Estado del menu y edición */
volatile menu_option_t menu_selected = MENU_SETPOINT;
volatile uint8_t editing_time = 0;
volatile uint8_t edit_field = 0;
rtc_time_t rtc_editing;


/* Nuevo: edición de setpoint */
volatile uint8_t editing_setpoint = 0;
float current_setpoint = 0.0f;   // valor mostrado y editado

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C2_Init(void);
static void MX_TIM1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void PWM_SetSpeed(uint32_t freq_hz){
	HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);	//Sabiendo que Preescaler = 15 siempre
	uint32_t periodo=(4500000/freq_hz)-1;		//Sale de hacer 72Mhz/(1+preescaler)(1+ARR)
	TIM1->ARR=periodo;
	TIM1->CCR2=periodo/2;
	//HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
}

void PWM_Stop(){
	HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
}
void PWM_Start(){
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
}
void sentido(int s){
	if(s==LEFT){
		HAL_GPIO_WritePin(PORT_IN1, PIN_IN1, GPIO_PIN_SET);
		HAL_GPIO_WritePin(PORT_IN2, PIN_IN2, GPIO_PIN_RESET);
	}else if(s==RIGHT){
		HAL_GPIO_WritePin(PORT_IN1, PIN_IN1, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(PORT_IN2, PIN_IN2, GPIO_PIN_SET);
	}else if(s==STOP){
		HAL_GPIO_WritePin(PORT_IN1, PIN_IN1, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(PORT_IN2, PIN_IN2, GPIO_PIN_RESET);
	}
}
void speed(uint32_t s){
	PWM_Stop();
	//MINIMA VELOCIDAD 20!!
	float period = (float)TIM1->ARR;
	float temp = 0;
	if(s>100) s = 100;
	if(s>0){
		if(s<=10) s=10;
		temp = (float) s;
		temp = temp/100;
		TIM1->CCR2=(uint32_t)(period * temp);
		PWM_Start();
	}
}

void I2C_controllerTask(void *pvParameters) {
    i2cQueue_t i2cq;
    uint8_t temp[8];
    while (1) {
        xQueueReceive(I2C_txQueue, &i2cq, portMAX_DELAY);
        if (i2cq.type == RECEIVE) {
            HAL_I2C_Master_Transmit(&hi2c2, i2cq.device_id, &i2cq.reg, 1, 10);
            HAL_I2C_Master_Receive(&hi2c2, i2cq.device_id, temp, i2cq.n_bytes, 10);
            xQueueSend(I2C_rxQueue, temp, portMAX_DELAY);
        } else {
            temp[0] = i2cq.reg;
            for (uint8_t i = 0; i < i2cq.n_bytes; i++) temp[i + 1] = i2cq.data[i];
            HAL_I2C_Master_Transmit(&hi2c2, i2cq.device_id, temp, i2cq.n_bytes + 1, 10);
        }
    }
}

void buttonHandlerTask(void *pvParameters) {
    TickType_t lastPress[4] = {0};
    while (1) {
        TickType_t now = xTaskGetTickCount();

        // BOTÓN 1 (sem_btn0): cambiar opción o guardar (menu/save)
        if (xSemaphoreTake(sem_btn0, 0) == pdTRUE && (now - lastPress[0] > pdMS_TO_TICKS(200))) {
            if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_3) == GPIO_PIN_RESET) {
                lastPress[0] = now;

                if (!editing_time && !editing_setpoint) {
                    // si no estamos editando, cambiar opción menú
                    menu_selected = (menu_selected + 1) % MENU_TOTAL_ITEMS;
                } else if (editing_time) {
                    // Guardar RTC en DS1307 (igual que antes)
                    uint8_t buf[7];
                    buf[0] = ((rtc_editing.sec/10)<<4)|(rtc_editing.sec%10);
                    buf[1] = ((rtc_editing.min/10)<<4)|(rtc_editing.min%10);
                    buf[2] = ((rtc_editing.hour/10)<<4)|(rtc_editing.hour%10);
                    buf[3] = 1;
                    buf[4] = ((rtc_editing.day/10)<<4)|(rtc_editing.day%10);
                    buf[5] = ((rtc_editing.month/10)<<4)|(rtc_editing.month%10);
                    buf[6] = ((rtc_editing.year/10)<<4)|(rtc_editing.year%10);
                    i2cQueue_t i2cq = {0x68 << 1, 0x00, TRANSMIT, 7, buf};
                    xQueueSend(I2C_txQueue, &i2cq, portMAX_DELAY);
                    editing_time = 0;
                } else if (editing_setpoint) {
                    // Guardar setpoint: tomar semáforo (si está disponible) y enviar a PID
                    if (xSemaphoreTake(goalangleSemphr, pdMS_TO_TICKS(50)) == pdTRUE) {
                        float sp = current_setpoint;
                        xQueueSend(goalangleQueue, &sp, portMAX_DELAY);
                        editing_setpoint = 0;
                        // queda pendiente: PID cuando termine dará el semáforo con xSemaphoreGive
                    }
                }
            }
        }

        // BOTÓN 2 (sem_btn1): entrar o cambiar campo (enter)
        if (xSemaphoreTake(sem_btn1, 0) == pdTRUE && (now - lastPress[1] > pdMS_TO_TICKS(200))) {
            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_RESET) {
                lastPress[1] = now;

                if (menu_selected == MENU_SET_TIME && !editing_time) {
                    xQueuePeek(rtcQueue, &rtc_editing, 0);
                    editing_time = 1;
                    edit_field = 0;
                } else if (editing_time) {
                    edit_field = (edit_field + 1) % 6;
                } else if (menu_selected == MENU_SETPOINT && !editing_setpoint) {
                    // entrar a editar setpoint
                    editing_setpoint = 1;
                    // current_setpoint ya contiene el valor actual a editar
                } else if (editing_setpoint) {
                    // si querés podés usar este botón para cambiar modos; aquí lo ignoramos
                }
            }
        }

        // BOTÓN 3 (sem_btn2): subir valor (subir setpoint o campo RTC)
        if (xSemaphoreTake(sem_btn2, 0) == pdTRUE && (now - lastPress[2] > pdMS_TO_TICKS(200))) {
            if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_RESET) {
                lastPress[2] = now;

                if (editing_time) {
                    switch (edit_field) {
                        case 0: if (++rtc_editing.hour > 23) rtc_editing.hour = 0; break;
                        case 1: if (++rtc_editing.min > 59)  rtc_editing.min = 0; break;
                        case 2: if (++rtc_editing.sec > 59)  rtc_editing.sec = 0; break;
                        case 3: if (++rtc_editing.day > 31)  rtc_editing.day = 1; break;
                        case 4: if (++rtc_editing.month > 12)rtc_editing.month = 1; break;
                        case 5: if (++rtc_editing.year > 99) rtc_editing.year = 0; break;
                    }
                } else if (editing_setpoint) {
                    current_setpoint += INC_STEP;
                    if (current_setpoint > 360.0f) current_setpoint = 360.0f; // tope
                }
            }
        }

        // BOTÓN 4 (sem_btn3): bajar valor (bajar setpoint o campo RTC)
        if (xSemaphoreTake(sem_btn3, 0) == pdTRUE && (now - lastPress[3] > pdMS_TO_TICKS(200))) {
            if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_RESET) {
                lastPress[3] = now;

                if (editing_time) {
                    switch (edit_field) {
                        case 0: rtc_editing.hour  = (rtc_editing.hour == 0) ? 23 : rtc_editing.hour - 1; break;
                        case 1: rtc_editing.min   = (rtc_editing.min == 0)  ? 59 : rtc_editing.min - 1; break;
                        case 2: rtc_editing.sec   = (rtc_editing.sec == 0)  ? 59 : rtc_editing.sec - 1; break;
                        case 3: rtc_editing.day   = (rtc_editing.day == 1)  ? 31 : rtc_editing.day - 1; break;
                        case 4: rtc_editing.month = (rtc_editing.month == 1)? 12 : rtc_editing.month - 1; break;
                        case 5: rtc_editing.year  = (rtc_editing.year == 0) ? 99 : rtc_editing.year - 1; break;
                    }
                } else if (editing_setpoint) {
                    current_setpoint -= INC_STEP;
                    if (current_setpoint < 0.0f) current_setpoint = 0.0f;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void menuTask(void *pvParameters) {
    lcd_init();
    lcd_clear();
    char buffer[17];
    TickType_t last_blink = 0;
    uint8_t blink_state = 1;
    uint8_t last_editing_time = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        if (now - last_blink >= pdMS_TO_TICKS(500)) {
            blink_state = !blink_state;
            last_blink = now;
        }

        if (!editing_time && !editing_setpoint) {
            // Mostrar menú y también el setpoint en la línea donde está Setpoint
            lcd_goto_XY(0, 0);
            if (menu_selected == MENU_SETPOINT) {
                lcd_send_string("> Setpoint    ");
            } else {
                lcd_send_string("  Setpoint    ");
            }

            // mostrar setpoint al final de la misma línea (ej: " 123.0")
            lcd_goto_XY(0, 12);
            snprintf(buffer, 6, "%5.1f", current_setpoint);
            lcd_send_string(buffer);

            lcd_goto_XY(1, 0);
            lcd_send_string(menu_selected == MENU_SET_TIME ? "> Set time/date" : "  Set time/date");

            lcd_goto_XY(2, 0);
            lcd_send_string(menu_selected == MENU_MARGEN ? "> Margen Setpoint  " : "  Margen Setpoint  ");

            lcd_goto_XY(3, 0);
            lcd_send_string(menu_selected == MENU_DATALOGGER ? "> Datalogger   " : "  Datalogger   ");

        } else if (editing_setpoint) {
            // Mostrar pantalla de edición de setpoint
            lcd_goto_XY(0, 0);
            lcd_send_string("Edit Setpoint:   ");
            lcd_goto_XY(1, 0);
            // blink cuando editando
            if (!blink_state) {
                lcd_send_string("      ----.-     ");
            } else {
                snprintf(buffer, sizeof(buffer), "   %6.1f       ", current_setpoint);
                lcd_send_string(buffer);
            }
            // limpiar lineas inferiores
            lcd_goto_XY(2,0); lcd_send_string("                    ");
            lcd_goto_XY(3,0); lcd_send_string(" Press MENU to save ");
        } else {
            // editing_time (ya existente)
            // Línea 0: Hora
            lcd_goto_XY(0, 0);
            if (edit_field == 0 && !blink_state)
                lcd_send_string("  ");
            else
                snprintf(buffer, sizeof(buffer), "%02u", rtc_editing.hour), lcd_send_string(buffer);

            lcd_send_string(":");

            if (edit_field == 1 && !blink_state)
                lcd_send_string("  ");
            else
                snprintf(buffer, sizeof(buffer), "%02u", rtc_editing.min), lcd_send_string(buffer);

            lcd_send_string(":");

            if (edit_field == 2 && !blink_state)
                lcd_send_string("  ");
            else
                snprintf(buffer, sizeof(buffer), "%02u", rtc_editing.sec), lcd_send_string(buffer);

            // Línea 1: Fecha
            lcd_goto_XY(1, 0);
            if (edit_field == 3 && !blink_state)
                lcd_send_string("  ");
            else
                snprintf(buffer, sizeof(buffer), "%02u", rtc_editing.day), lcd_send_string(buffer);

            lcd_send_string("/");

            if (edit_field == 4 && !blink_state)
                lcd_send_string("  ");
            else
                snprintf(buffer, sizeof(buffer), "%02u", rtc_editing.month), lcd_send_string(buffer);

            lcd_send_string("/");

            if (edit_field == 5 && !blink_state)
                lcd_send_string("  ");
            else
                snprintf(buffer, sizeof(buffer), "%02u", rtc_editing.year), lcd_send_string(buffer);
        }

        if (editing_time != last_editing_time) {
            lcd_goto_XY(0, 0);
            lcd_send_string("                    ");  // Borra línea 0
            lcd_goto_XY(1, 0);
            lcd_send_string("                    ");  // Borra línea 1
            lcd_goto_XY(2, 0);
            lcd_send_string("                    ");  // Borra línea 2
            lcd_goto_XY(3, 0);
            lcd_send_string("                    ");  // Borra línea 3
            last_editing_time = editing_time;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void PID_Task(void *pvParameters) {
	//velocidad=kp*e+ki*integral(e*dt)+kd*d(e)/dt
	//e=setpoint - curr_angle

	double kp = 1.2;  //ajustar valor
	double ki = 0.05; //ajustar valor
	double kd = 0.3;  //ajustar valor

	// Variables de cálculo
	double error=0;
	double errSum=0;
	double dErr=0;
	double last_error=0; //error de la "pasada" anterior del pid
	double dt=0;
	double y_out=0;

	// Control
	int rotation=STOP;
    int running_flag=PID_STOP;
	float goal_angle=0;
    float curr_angle=0;
    float angle_error=10; //error maximo esperado, para tener rangos setpoint+-error
    //recibo angle_error tambien de una queue (la misma que el sp)

    TickType_t curr_ticks = xTaskGetTickCount();
    TickType_t last_ticks = curr_ticks;

    sentido(STOP);
    PWM_SetSpeed(1000); // 1 kHz de PWM

    while (1) {
    	if(running_flag==PID_STOP){
    		sentido(STOP);
    		 // Espera nuevo setpoint
  		    xQueueReceive(goalangleQueue, &goal_angle, portMAX_DELAY);//espero a recibir angulo a rotar
  		  // cuando menu guarda, ya tomó goalangleSemphr; aca se arranca
  		    running_flag = PID_RUN;
  		    last_ticks = xTaskGetTickCount();
  		    errSum = 0;
  		    last_error = 0;

    	}
    	if(running_flag==PID_RUN){
    		curr_ticks = xTaskGetTickCount();
    		dt = (double)(curr_ticks - last_ticks) / 1000.0; // ms → s

    		if(dt<=0){
    			dt=0.001;
    		}

    		last_ticks = curr_ticks;

    		            // Lee ángulo actual sin consumirlo
    	   	if(xQueuePeek(angleQueue, &curr_angle, pdMS_TO_TICKS(50)) != pdTRUE){

    	   	 vTaskDelay(pdMS_TO_TICKS(10));  // si no hay lectura, esperar un poco
    	   	 continue;
    	   	}

    		            // Calcula error
    		            error = (double)(goal_angle - curr_angle);
    		            errSum += error * dt;
    		            dErr = (error - last_error) / dt;
    		            last_error = error;

    		            // Salida PID
    		            y_out = kp * error + ki * errSum + kd * dErr;

    		            // Dirección y magnitud
    		            if (y_out < 0) {
    		                rotation = LEFT;
    		                y_out = -y_out;
    		            } else {
    		                rotation = RIGHT;
    		            }

    		            // Limitar salida
    		            if (y_out > 100) y_out = 100;
    		            if (y_out < 10)  y_out = 10; // velocidad mínima

    		            sentido(rotation);
    		            speed((uint32_t)y_out);

    		            // Verificar si está dentro de rango
    		            if (fabs(error) <= angle_error) {
    		                PWM_Stop();
    		                running_flag = PID_STOP;
    		                xSemaphoreGive(goalangleSemphr); // liberar semáforo para permitir nueva edición en menú
    		            }
    		        }
    	vTaskDelay(pdMS_TO_TICKS(50));
//
//    	xSemaphoreTake(goalangleSemphr,portMAX_DELAY); //corroborar que la botonera siempre tome el semaforo primero con takefromISR
//    	//uso peek para permitir entrar variar veces sin bloquear, recordar limpiar queue cuando termine
//    	xQueuePeek(goalangleQueue, &goal_angle, portMAX_DELAY);	//espero a recibir angulo a rotar
//        xQueuePeek(angleQueue, &curr_angle, portMAX_DELAY);		//espero hasta tener un valor de angulo actual
//        //si no estoy dentro del rango deseado
//        if((curr_angle<(goal_angle-angle_error))||(curr_angle>(goal_angle+angle_error))){
//        	sentido(LEFT);
//        	speed(50);	//speed permite cambiar velocidad durante funcionamiento
//        }else{
//        	PWM_Stop();
//        	xSemaphoreGive(goalangleSemphr);
//        }
//        //dentro del if iria la logica de pid
//        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
//la tarea espera a que desde el menú se la habilite y recibe el valor de la misma.
//x ahora solo recibe el valor,y corrobora si se tiene o no que mover el motor para llegar
//de ser necesario, se puede medir el tiempo entre mediciones utilizando el TIM3 o TIM4, para tener precision de uS



void as5600_readerTask(void *pvParameters) {
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    uint8_t raw_data[2];
    float angle_deg;

    i2cQueue_t i2cq;
    i2cq.device_id = 0x36 << 1;
    i2cq.reg = 0x0E;  // ANGLE register (MSB)
    i2cq.type = RECEIVE;
    i2cq.n_bytes = 2;
    i2cq.data = raw_data;

    while (1) {
        xQueueSend(I2C_txQueue, &i2cq, portMAX_DELAY);
        xQueueReceive(I2C_rxQueue, raw_data, portMAX_DELAY);

        uint16_t raw_angle = ((uint16_t)raw_data[0] << 8) | raw_data[1];
        angle_deg = (float)raw_angle * 360.0f / 4096.0f;

        xQueueSend(angleQueue, &angle_deg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void EXTI3_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(sem_btn0, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
void EXTI4_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(sem_btn3, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
void EXTI9_5_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(sem_btn2, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
void EXTI15_10_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(sem_btn1, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
/*
void lcd_displayTask(void *pvParameters) {
    float angle;
    char buffer[20];

    lcd_init();
    lcd_clear();

    while (1) {
        xQueueReceive(angleQueue, &angle, portMAX_DELAY);

        snprintf(buffer, sizeof(buffer), "Angulo: %-6.1f", angle);
        lcd_goto_XY(2, 0);
        lcd_send_string(buffer);
    }
}



*/
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C2_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */

 //   sentido(STOP);
 //   PWM_SetSpeed(1000);
 //   speed(20);



    //min 25 con 2khz
    //min 18 recomiendo 20 con 1khz
    //min 14 con 500hz
    //min 10 con 100hz --> frec minima


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  /* semáforos botones */

     sem_btn0 = xSemaphoreCreateBinary();
     sem_btn1 = xSemaphoreCreateBinary();
     sem_btn2 = xSemaphoreCreateBinary();
     sem_btn3 = xSemaphoreCreateBinary();

     /* colas */
     I2C_txQueue = xQueueCreate(8, sizeof(i2cQueue_t));
     I2C_rxQueue = xQueueCreate(8, sizeof(uint8_t) * 8);
     angleQueue  = xQueueCreate(2, sizeof(float));
     rtcQueue = xQueueCreate(2, sizeof(rtc_time_t));
     goalangleQueue = xQueueCreate(2, sizeof(float));

     /* semáforo para bloquear edición hasta que PID termine */
     goalangleSemphr = xSemaphoreCreateBinary();

     // lo damos para que inicialmente pueda editar y guardar
      xSemaphoreGive(goalangleSemphr);


     xTaskCreate(I2C_controllerTask, "I2C_Guardian", 512, NULL, 3, NULL);
     xTaskCreate(buttonHandlerTask, "Buttons      ", 256, NULL, 2, NULL);
     xTaskCreate(PID_Task          , "PID"         , 512, NULL, 2, NULL);
     xTaskCreate(as5600_readerTask , "AS5600"      , 256, NULL, 2, NULL);
     xTaskCreate(menuTask          , "Menu"        , 512, NULL, 1, NULL);


     vTaskStartScheduler();

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000;
  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 15;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED_Pin|IN1_Pin|IN2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

  /*Configure GPIO pins : LED_Pin IN1_Pin IN2_Pin */
  GPIO_InitStruct.Pin = LED_Pin|IN1_Pin|IN2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB3 PB4 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
