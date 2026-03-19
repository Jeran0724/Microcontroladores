//Sistema de Porton Automatico 

#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/timers.h"
#include "esp_timer.h"

static const char *tag = "GARAGE";

//Estados
#define Estado_Inicio 0
#define Estado_Desconocido 1
#define Estado_Cerrando 2
#define Estado_Abriendo 3
#define Estado_Cerrado 4
#define Estado_Abierto 5
#define Estado_Parado_Objeto 6
#define Estado_Parado_Usuario 7
#define Estado_Error 8

//Salidas
#define Motor_ON 1
#define Motor_OFF 0
#define Lamp_ON 1
#define Lamp_OFF 0
#define Buzzer_ON 1
#define Buzzer_OFF 0

//Tiempo máximo motor
#define Run_time 180000000

//Estructura IO
struct signal {

    unsigned int fca;
    unsigned int fcc;
    unsigned int ftc;

    unsigned int bc;
    unsigned int ba;
    unsigned int bs;
    unsigned int be;
    unsigned int pp;

    unsigned int mc;
    unsigned int ma;
    unsigned int lamp;
    unsigned int buzzer;

} io;


//Variables FSM
int Estado_Actual = Estado_Inicio;
int Estado_Siguiente = Estado_Inicio;
int Estado_Anterior = Estado_Parado_Usuario;

int pp_prev = 0;
int flanco_pp = 0;

int64_t motor_start_time = 0;

//Timer
TimerHandle_t xTimers;
int interval = 100;


//===================== TIMER =====================

void vTimerCallback(TimerHandle_t pxTimer)
{
    flanco_pp = (io.pp == 1 && pp_prev == 0);
    pp_prev = io.pp;
}

//=================================================


//===================== ESTADOS ===================

void Func_Estado_Inicio(void)
{
    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_OFF;
    io.buzzer = Buzzer_OFF;

    motor_start_time = 0;

    Estado_Siguiente = Estado_Desconocido;
}

void Func_Estado_Desconocido(void)
{

    io.mc = Motor_ON;
    io.ma = Motor_OFF;
    io.lamp = Lamp_OFF;
    io.buzzer = Buzzer_ON;

    if (motor_start_time == 0)
        motor_start_time = esp_timer_get_time();

    else if (io.fcc)
    {
        Estado_Siguiente = Estado_Cerrado;
        motor_start_time = 0;
    }

    else if ((esp_timer_get_time() - motor_start_time) > Run_time)
        Estado_Siguiente = Estado_Error;
}

void Func_Estado_Cerrando(void)
{

    io.mc = Motor_ON;
    io.ma = Motor_OFF;
    io.lamp = Lamp_ON;
    io.buzzer = Buzzer_ON;

    if (motor_start_time == 0)
        motor_start_time = esp_timer_get_time();

    else if (io.fcc)
    {
        Estado_Siguiente = Estado_Cerrado;
        motor_start_time = 0;
    }

    else if (io.ftc)
    {
        Estado_Siguiente = Estado_Parado_Objeto;
        motor_start_time = 0;
    }

    else if (flanco_pp || io.bs)
    {
        Estado_Siguiente = Estado_Parado_Usuario;
        motor_start_time = 0;
    }

    else if ((esp_timer_get_time() - motor_start_time) > Run_time)
        Estado_Siguiente = Estado_Error;
}

void Func_Estado_Abriendo(void)
{

    io.mc = Motor_OFF;
    io.ma = Motor_ON;
    io.lamp = Lamp_ON;
    io.buzzer = Buzzer_ON;

    if (motor_start_time == 0)
        motor_start_time = esp_timer_get_time();

    else if (io.fca)
    {
        Estado_Siguiente = Estado_Abierto;
        motor_start_time = 0;
    }

    else if (flanco_pp || io.bs)
    {
        Estado_Siguiente = Estado_Parado_Usuario;
        motor_start_time = 0;
    }

    else if ((esp_timer_get_time() - motor_start_time) > Run_time)
        Estado_Siguiente = Estado_Error;
}

void Func_Estado_Cerrado(void)
{

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_OFF;
    io.buzzer = Buzzer_OFF;

    if (flanco_pp || io.ba)
        Estado_Siguiente = Estado_Abriendo;
}

void Func_Estado_Abierto(void)
{

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_OFF;
    io.buzzer = Buzzer_OFF;

    if (flanco_pp || io.bc)
        Estado_Siguiente = Estado_Cerrando;
}

void Func_Estado_Parado_Objeto(void)
{

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_ON;
    io.buzzer = Buzzer_OFF;

    if (flanco_pp || io.ba)
        Estado_Siguiente = Estado_Abriendo;
}

void Func_Estado_Parado_Usuario(void)
{

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_ON;
    io.buzzer = Buzzer_OFF;

    if (flanco_pp)
    {
        if (Estado_Anterior == Estado_Cerrando)
            Estado_Siguiente = Estado_Abriendo;
        else
            Estado_Siguiente = Estado_Cerrando;
    }
}

void Func_Estado_Error(void)
{

    io.mc = Motor_OFF;
    io.ma = Motor_OFF;
    io.lamp = Lamp_ON;
    io.buzzer = Buzzer_ON;

    if (io.be)
        Estado_Siguiente = Estado_Inicio;
}


//=================================================


//==================== MAIN =======================

void app_main(void)
{

    ESP_LOGI(tag, "Sistema de porton iniciado");

    xTimers = xTimerCreate(
                "Timer",
                pdMS_TO_TICKS(interval),
                pdTRUE,
                NULL,
                vTimerCallback);

    xTimerStart(xTimers, 0);


    while (1)
    {

        Estado_Anterior = Estado_Actual;
        Estado_Actual = Estado_Siguiente;

        switch (Estado_Actual)
        {

            case Estado_Inicio:
                Func_Estado_Inicio();
                break;

            case Estado_Desconocido:
                Func_Estado_Desconocido();
                break;

            case Estado_Cerrando:
                Func_Estado_Cerrando();
                break;

            case Estado_Abriendo:
                Func_Estado_Abriendo();
                break;

            case Estado_Cerrado:
                Func_Estado_Cerrado();
                break;

            case Estado_Abierto:
                Func_Estado_Abierto();
                break;

            case Estado_Parado_Objeto:
                Func_Estado_Parado_Objeto();
                break;

            case Estado_Parado_Usuario:
                Func_Estado_Parado_Usuario();
                break;

            case Estado_Error:
                Func_Estado_Error();
                break;

            default:
                ESP_LOGE(tag, "Estado desconocido");
                Estado_Siguiente = Estado_Inicio;
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}