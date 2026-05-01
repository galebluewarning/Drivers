#include "App_Control.h"
#include "App_SHT40.h"  // 需要获取 sht40.humidity
#include <stdio.h>

/* ============================================================ */
/* ====================   硬件配置宏定义   ==================== */
/* ============================================================ */

/* --- 1. 输入：钮子开关 (PA5 - PA8) --- 
EXTI5 ~ EXTI9 全部共享同一个中断向量 EXTI9_5_IRQHandler*/
#define SW_PORT         GPIOA
#define PIN_SW_MASTER   GPIO_PIN_8  // 总开关 (EXTI8)
#define PIN_SW_LED      GPIO_PIN_7  // LED开关 (EXTI7)
#define PIN_SW_PUMP     GPIO_PIN_6  // 水泵开关 (EXTI6)
#define PIN_SW_AUX      GPIO_PIN_5  // 备用开关 (EXTI5)

/* --- 2. 输出：NMOS 控制 (PB12 - PB14) --- */
#define CTRL_PORT       GPIOB
#define PIN_PUMP        GPIO_PIN_12 // 水泵
#define PIN_LED         GPIO_PIN_13 // LED
#define PIN_FAN         GPIO_PIN_14 // 风扇

/* 输出控制宏 (高电平开启，低电平关闭) */
#define PUMP_ON()       HAL_GPIO_WritePin(CTRL_PORT, PIN_PUMP, GPIO_PIN_SET)
#define PUMP_OFF()      HAL_GPIO_WritePin(CTRL_PORT, PIN_PUMP, GPIO_PIN_RESET)

#define LED_ON()        HAL_GPIO_WritePin(CTRL_PORT, PIN_LED, GPIO_PIN_SET)
#define LED_OFF()       HAL_GPIO_WritePin(CTRL_PORT, PIN_LED, GPIO_PIN_RESET)

#define FAN_ON()        HAL_GPIO_WritePin(CTRL_PORT, PIN_FAN, GPIO_PIN_SET)
#define FAN_OFF()       HAL_GPIO_WritePin(CTRL_PORT, PIN_FAN, GPIO_PIN_RESET)

/* ============================================================ */

/* 全局开关状态标志位 */
volatile uint8_t Flag_Master = 0;
volatile uint8_t Flag_LED    = 0;
volatile uint8_t Flag_Pump   = 0;
volatile uint8_t Flag_Aux    = 0;

/* 引用外部 SHT40 实例 (定义在 main.c 中) */
extern SHT40_t sht40;

/* 内部辅助：读取单个引脚状态 (上拉输入: High=1, Low=0) */
static uint8_t Read_Sw_State(uint16_t pin) {
    return (HAL_GPIO_ReadPin(SW_PORT, pin) == GPIO_PIN_SET) ? 1 : 0;
}

/**
 * @brief 初始化：读取初始开关状态 + 复位输出
 */
void App_Control_Init(void) {
    // 1. 读取开关物理状态
    Flag_Master = Read_Sw_State(PIN_SW_MASTER);
    Flag_LED    = Read_Sw_State(PIN_SW_LED);
    Flag_Pump   = Read_Sw_State(PIN_SW_PUMP);
    Flag_Aux    = Read_Sw_State(PIN_SW_AUX);
    
    // 2. 强制关闭所有外设 (安全起见)
    PUMP_OFF();
    LED_OFF();
    FAN_OFF();

    printf("[App_Control Init:Master:%d,LED:%d,Pump:%d,Aux:%d]\r\n", 
           Flag_Master, Flag_LED, Flag_Pump, Flag_Aux);
}

/**
 * @brief 外部中断处理：实时更新开关标志位
 * 覆盖 PA5, PA6, PA7, PA8
 */
void App_Control_HandleEXTI(uint16_t GPIO_Pin) {
    switch (GPIO_Pin) {
        case PIN_SW_MASTER:
            Flag_Master = Read_Sw_State(PIN_SW_MASTER);
            break;
        case PIN_SW_LED:
            Flag_LED = Read_Sw_State(PIN_SW_LED);
            break;
        case PIN_SW_PUMP:
            Flag_Pump = Read_Sw_State(PIN_SW_PUMP);
            break;
        case PIN_SW_AUX:
            Flag_Aux = Read_Sw_State(PIN_SW_AUX);
            break;
        default:
            break;
    }
}

/**
 * @brief 核心控制逻辑 (建议 100ms - 1000ms 调用一次)
 */
void App_Control_Process(void) {
    
    /* === 第一级：总开关逻辑 (Master) === */
    if (Flag_Master == 1) 
    {
        /* ---------------- A. 水泵控制 (Pump) ---------------- */
        if (Flag_Pump == 1) 
        {
            // [自动逻辑] 滞回区间控制
            // 湿度 < 70% -> 开
            if (sht40.humidity < 70.0f) {
                PUMP_ON();
            }
            // 湿度 > 85% -> 关
            else if (sht40.humidity > 85.0f) {
                PUMP_OFF();
            }
            // 70% ~ 85% 之间保持原状态，不动作
        }
        else 
        {
            // [手动逻辑] 水泵开关关闭 -> 强制关
            PUMP_OFF();
        }

        /* ---------------- B. LED 控制 (LED) ----------------- */
        if (Flag_LED == 1) {
            LED_ON();
        } else {
            LED_OFF();
        }

        /* ---------------- C. 风扇控制 (Fan) ----------------- */
        // [自动逻辑] 湿度 > 88% 开
        if (sht40.humidity > 88.0f) {
            FAN_ON();
        } 
        // 滞回关闭：湿度 < 85% 关 (防止临界点频繁启停)
        else if (sht40.humidity < 85.0f) {
            FAN_OFF();
        }
        
        /* ---------------- D. 备用开关逻辑 (Aux) -------------- */
        // 目前暂无逻辑，可在此处添加
        // 例如：if (Flag_Aux) { ... }
    }
    else 
    {
        /* === 总开关关闭 (Master == 0) === */
        /* 最高优先级：强制切断所有负载 */
        PUMP_OFF();
        LED_OFF();
        FAN_OFF();
    }
}