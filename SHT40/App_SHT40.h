#ifndef __APP_SHT40_H__
#define __APP_SHT40_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h" 
#include "i2c.h" 


/* SHT40 I2C Address (7-bit) */
#define SHT40_I2C_ADDR (0x44 << 1) 

/* Commands */
#define SHT40_CMD_HIGH_PRECISION 0xFD
#define SHT40_CMD_MED_PRECISION  0xF6
#define SHT40_CMD_LOW_PRECISION  0xE0
#define SHT40_CMD_SOFT_RESET     0x94
/* 加热器命令 (200mW, 1秒) */
/* SHT40 加热指令是 16位 的，需拆分发送 */
#define SHT40_CMD_HEATER_200MW_1S_MSB  0x39
#define SHT40_CMD_HEATER_200MW_1S_LSB  0xC6

/* Data Structure */
typedef struct {
    I2C_HandleTypeDef *i2cHandle; // 绑定的 I2C 句柄
    float temperature;            // 温度值 (℃)
    float humidity;               // 湿度值 (%RH)
    /* 环境监控阈值变量 */
    float temp_bot;  // 温度下限
    float temp_top;  // 温度上限
    float humi_bot;  // 湿度下限
    float humi_top;  // 湿度上限
} SHT40_t;

/* 函数原型声明 */
/**
 * @brief 初始化 SHT40 设备结构体并设置默认阈值
 * @param dev       指向 SHT40 设备实例的指针，用于存储硬件句柄及环境参数
 * @param i2cHandle 指向 STM32 HAL 库定义的 I2C 总线句柄（如 &hi2c1）
 */
void App_SHT40_Init(SHT40_t *dev, I2C_HandleTypeDef *i2cHandle);

/**
 * @brief 执行 SHT40 传感器的软件复位
 * @note  调用后会通过 I2C 发送 0x94 指令，并包含必要的延时（约 10ms）以等待传感器重启完成
 * @param dev       指向待复位的 SHT40 设备实例的指针
 */
void App_SHT40_SoftReset(SHT40_t *dev);

/**
 * @brief 读取当前温湿度数据（采用高精度测量模式）
 * @details 该函数执行以下流程：发送测量指令 -> 硬件延时 -> 读取 6 字节原始数据 -> CRC 校验 -> 物理量转换
 * @param dev       指向 SHT40 设备实例的指针，读取到的温度和湿度将更新至其成员变量中
 * @return HAL_StatusTypeDef 返回 HAL_OK 表示读取及 CRC 校验均成功，否则返回 HAL_ERROR
 */
HAL_StatusTypeDef App_SHT40_ReadTempHum(SHT40_t *dev);

/**
 * @brief 激活 SHT40 内置加热器 (去凝露模式)
 * @details 发送 200mW 加热 1秒 指令，并阻塞等待 1.1秒。
 * 用于在极端高湿环境下去除传感器表面的冷凝水。
 * @warning 加热后温度会短暂升高，湿度降低，建议配合 30秒 的数据冷却屏蔽期使用。
 * @param dev 指向 SHT40 设备实例的指针
 * @return HAL_StatusTypeDef 发送结果
 */
HAL_StatusTypeDef App_SHT40_ActivateHeater(SHT40_t *dev);

/**
 * @brief 通过串口指令解析并设置阈值，支持写入 BKP 备份寄存器
*/
void App_SHT40_ParseCommand(SHT40_t *dev, char *cmd_line);

/**
 * @brief  封装函数：读取并打印温湿度
 * @param  dev: 传感器结构体指针
 */
void App_SHT40_Print(SHT40_t *dev); 

/**
 * @brief 周期性非阻塞读取并打印数据
 * @param dev         SHT40 设备实例指针
 * @param interval_ms 读取间隔 (毫秒)
 * @eg App_SHT40_NEWS(&sht40, 1000);
 */
void App_SHT40_NEWS(SHT40_t *dev, uint32_t interval_ms);

#ifdef __cplusplus
}
#endif
#endif /* __APP_SHT40_H__ */