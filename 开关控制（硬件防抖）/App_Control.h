/*
 * App_Control.h
 * * 功能整合：
 * 1. 输入管理：读取 4 个钮子开关 (PA5-PA8) 的状态
 * 2. 输出管理：控制 3 个外设 (PB12-PB14) 的开关
 * 3. 核心逻辑：自动/手动控制策略
 */

#ifndef __APP_CONTROL_H__
#define __APP_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* --- 全局开关状态标志位 (供调试或显示使用) --- */
/* 1 = ON/运行, 0 = OFF/停止 */
extern volatile uint8_t Flag_Master; // 总开关 (PA8)
extern volatile uint8_t Flag_LED;    // LED开关 (PA7)
extern volatile uint8_t Flag_Pump;   // 水泵开关 (PA6)
extern volatile uint8_t Flag_Aux;    // 备用开关 (PA5)

/**
 * @brief 控制系统初始化
 * 1. 读取开关初始位置
 * 2. 将所有外设复位到关闭状态
 * @note 请在 main 的 while(1) 之前调用
 */
void App_Control_Init(void);

/**
 * @brief 核心控制逻辑任务
 * @note  请在 main 的 while(1) 中周期调用
 * 根据开关状态 + 传感器数据 -> 控制外设
 */
void App_Control_Process(void);

/**
 * @brief 外部中断处理入口
 * @note  请在 HAL_GPIO_EXTI_Callback 中调用
 * @param GPIO_Pin 触发中断的引脚号
 */
void App_Control_HandleEXTI(uint16_t GPIO_Pin);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONTROL_H__ */