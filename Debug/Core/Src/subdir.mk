################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/cvout.c \
../Core/Src/dpot_mcp41hv.c \
../Core/Src/footswitch.c \
../Core/Src/led.c \
../Core/Src/led_demo.c \
../Core/Src/led_rgb.c \
../Core/Src/main.c \
../Core/Src/midi.c \
../Core/Src/pot.c \
../Core/Src/stm32f1xx_hal_msp.c \
../Core/Src/stm32f1xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f1xx.c 

OBJS += \
./Core/Src/cvout.o \
./Core/Src/dpot_mcp41hv.o \
./Core/Src/footswitch.o \
./Core/Src/led.o \
./Core/Src/led_demo.o \
./Core/Src/led_rgb.o \
./Core/Src/main.o \
./Core/Src/midi.o \
./Core/Src/pot.o \
./Core/Src/stm32f1xx_hal_msp.o \
./Core/Src/stm32f1xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f1xx.o 

C_DEPS += \
./Core/Src/cvout.d \
./Core/Src/dpot_mcp41hv.d \
./Core/Src/footswitch.d \
./Core/Src/led.d \
./Core/Src/led_demo.d \
./Core/Src/led_rgb.d \
./Core/Src/main.d \
./Core/Src/midi.d \
./Core/Src/pot.d \
./Core/Src/stm32f1xx_hal_msp.d \
./Core/Src/stm32f1xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f1xx.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F105xC -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/cvout.cyclo ./Core/Src/cvout.d ./Core/Src/cvout.o ./Core/Src/cvout.su ./Core/Src/dpot_mcp41hv.cyclo ./Core/Src/dpot_mcp41hv.d ./Core/Src/dpot_mcp41hv.o ./Core/Src/dpot_mcp41hv.su ./Core/Src/footswitch.cyclo ./Core/Src/footswitch.d ./Core/Src/footswitch.o ./Core/Src/footswitch.su ./Core/Src/led.cyclo ./Core/Src/led.d ./Core/Src/led.o ./Core/Src/led.su ./Core/Src/led_demo.cyclo ./Core/Src/led_demo.d ./Core/Src/led_demo.o ./Core/Src/led_demo.su ./Core/Src/led_rgb.cyclo ./Core/Src/led_rgb.d ./Core/Src/led_rgb.o ./Core/Src/led_rgb.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/midi.cyclo ./Core/Src/midi.d ./Core/Src/midi.o ./Core/Src/midi.su ./Core/Src/pot.cyclo ./Core/Src/pot.d ./Core/Src/pot.o ./Core/Src/pot.su ./Core/Src/stm32f1xx_hal_msp.cyclo ./Core/Src/stm32f1xx_hal_msp.d ./Core/Src/stm32f1xx_hal_msp.o ./Core/Src/stm32f1xx_hal_msp.su ./Core/Src/stm32f1xx_it.cyclo ./Core/Src/stm32f1xx_it.d ./Core/Src/stm32f1xx_it.o ./Core/Src/stm32f1xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f1xx.cyclo ./Core/Src/system_stm32f1xx.d ./Core/Src/system_stm32f1xx.o ./Core/Src/system_stm32f1xx.su

.PHONY: clean-Core-2f-Src

