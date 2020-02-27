/****************************************************************************
 *
 *   Copyright (c) 2020 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#pragma once

#include <lib/drivers/accelerometer/PX4Accelerometer.hpp>
#include <lib/perf/perf_counter.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <lib/drivers/device/spi.h>

#include "Bosch_BMI088_Accelerometer_Registers.hpp"

namespace Bosch_BMI088_Accelerometer
{

class BMI088_Accelerometer : public device::SPI, public px4::ScheduledWorkItem
{
public:
	BMI088_Accelerometer(int bus, uint32_t device, enum Rotation rotation);
	~BMI088_Accelerometer() override;

	bool Init();
	void Start();
	void Stop();
	bool Reset();
	void PrintInfo();

private:
	// Sensor Configuration
	static constexpr uint32_t ACCEL_RATE{1600}; // 1600 Hz accel
	static constexpr uint32_t FIFO_MAX_SAMPLES{ math::min(FIFO::SIZE / sizeof(FIFO::DATA) + 1, sizeof(PX4Accelerometer::FIFOSample::x) / sizeof(PX4Accelerometer::FIFOSample::x[0]))};

	// Transfer data
	struct TransferBuffer {
		uint8_t cmd;
		uint8_t dummy;
		FIFO::DATA f[FIFO_MAX_SAMPLES];
	};
	// ensure no struct padding
	static_assert(sizeof(TransferBuffer) == (2 * sizeof(uint8_t) + FIFO_MAX_SAMPLES *sizeof(FIFO::DATA)));

	struct register_config_t {
		Register reg;
		uint8_t set_bits{0};
		uint8_t clear_bits{0};
	};

	int probe() override;

	void Run() override;

	bool Configure();
	void ConfigureAccel();
	void ConfigureSampleRate(int sample_rate);

	static int DataReadyInterruptCallback(int irq, void *context, void *arg);
	void DataReady();
	bool DataReadyInterruptConfigure();
	bool DataReadyInterruptDisable();

	bool RegisterCheck(const register_config_t &reg_cfg, bool notify = false);

	uint8_t RegisterRead(Register reg);
	void RegisterWrite(Register reg, uint8_t value);
	void RegisterSetAndClearBits(Register reg, uint8_t setbits, uint8_t clearbits);
	void RegisterSetBits(Register reg, uint8_t setbits);
	void RegisterClearBits(Register reg, uint8_t clearbits);

	uint16_t FIFOReadCount();
	bool FIFORead(const hrt_abstime &timestamp_sample, uint16_t samples);
	void FIFOReset();

	void UpdateTemperature();

	uint8_t *_dma_data_buffer{nullptr};

	PX4Accelerometer _px4_accel;

	perf_counter_t _transfer_perf{perf_alloc(PC_ELAPSED, MODULE_NAME": accel transfer")};
	perf_counter_t _bad_register_perf{perf_alloc(PC_COUNT, MODULE_NAME": accel bad register")};
	perf_counter_t _bad_transfer_perf{perf_alloc(PC_COUNT, MODULE_NAME": accel bad transfer")};
	perf_counter_t _fifo_empty_perf{perf_alloc(PC_COUNT, MODULE_NAME": accel FIFO empty")};
	perf_counter_t _fifo_overflow_perf{perf_alloc(PC_COUNT, MODULE_NAME": accel FIFO overflow")};
	perf_counter_t _fifo_reset_perf{perf_alloc(PC_COUNT, MODULE_NAME": accel FIFO reset")};
	perf_counter_t _drdy_interval_perf{perf_alloc(PC_INTERVAL, MODULE_NAME": accel DRDY interval")};

	hrt_abstime _reset_timestamp{0};
	hrt_abstime _last_config_check_timestamp{0};
	hrt_abstime _fifo_watermark_interrupt_timestamp{0};
	hrt_abstime _temperature_update_timestamp{0};

	px4::atomic<uint8_t> _fifo_read_samples{0};
	bool _data_ready_interrupt_enabled{false};
	uint8_t _checked_register{0};

	enum class STATE : uint8_t {
		RESET,
		WAIT_FOR_RESET,
		CONFIGURE,
		FIFO_READ,
		REQUEST_STOP,
		STOPPED,
	};

	px4::atomic<STATE> _state{STATE::RESET};

	uint16_t _fifo_empty_interval_us{800}; // 1250 us / 800 Hz transfer interval
	uint8_t _fifo_accel_samples{static_cast<uint8_t>(_fifo_empty_interval_us / (1000000 / ACCEL_RATE))};

	static constexpr uint8_t size_register_cfg{8};
	register_config_t _register_cfg[size_register_cfg] {
		// Register                     | Set bits, Clear bits
		{ Register::ACC_PWR_CONF,       0, ACC_PWR_CONF_BIT::acc_pwr_save },
		{ Register::ACC_PWR_CTRL,       ACC_PWR_CTRL_BIT::acc_enable, 0 },
		{ Register::ACC_CONF,           ACC_CONF_BIT::acc_odr_1600, 0 },
		{ Register::ACC_RANGE,          ACC_RANGE_BIT::acc_range_24g, 0 },
		{ Register::FIFO_WTM_0,         0, 0 },
		{ Register::FIFO_WTM_1,         0, 0 },
		{ Register::FIFO_CONFIG_0,      FIFO_CONFIG_0_BIT::FIFO_mode, 0 },
		{ Register::FIFO_CONFIG_1,      FIFO_CONFIG_1_BIT::Acc_en, 0 },
		//{ Register::INT1_IO_CONF,       INT1_IO_CONF_BIT::int1_out, 0 },
		//{ Register::INT1_INT2_MAP_DATA, INT1_INT2_MAP_DATA_BIT::int1_fwm, 0}
	};
};

} // namespace Bosch_BMI088_Accelerometer
