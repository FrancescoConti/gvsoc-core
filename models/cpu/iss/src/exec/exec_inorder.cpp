/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 */

#include <vp/vp.hpp>
#include "iss.hpp"

Exec::Exec(Iss &iss)
    : iss(iss)
{
}

void Exec::build()
{
    this->iss.traces.new_trace("exec", &this->trace, vp::DEBUG);

    this->iss.new_master_port("busy", &busy_itf);

    flush_cache_ack_itf.set_sync_meth(&Exec::flush_cache_ack_sync);
    this->iss.new_slave_port(this, "flush_cache_ack", &flush_cache_ack_itf);
    this->iss.new_master_port("flush_cache_req", &flush_cache_req_itf);

    bootaddr_itf.set_sync_meth(&Exec::bootaddr_sync);
    this->iss.new_slave_port(this, "bootaddr", &bootaddr_itf);

    clock_itf.set_sync_meth(&Exec::clock_sync);
    this->iss.new_slave_port(this, "clock", &clock_itf);

    fetchen_itf.set_sync_meth(&Exec::fetchen_sync);
    this->iss.new_slave_port(this, "fetchen", &fetchen_itf);


}

void Exec::reset(bool active)
{
    this->clock_active = false;
}

void Exec::dbg_unit_step_check()
{
    if (this->iss.step_mode.get() && !this->iss.state.debug_mode)
    {
        this->iss.do_step.set(false);
        this->iss.hit_reg |= 1;
        if (this->iss.gdbserver.gdbserver)
        {
            this->iss.halted.set(true);
            this->iss.gdbserver.gdbserver->signal(&this->iss.gdbserver);
        }
        else
        {
            this->iss.dbgunit.set_halt_mode(true, HALT_CAUSE_STEP);
        }
    }
}

void Exec::exec_instr(void *__this, vp::clock_event *event)
{
    Exec *_this = (Exec *)__this;
    Iss *iss = &_this->iss;

    _this->trace.msg(vp::trace::LEVEL_TRACE, "Handling instruction with fast handler (insn_cycles: %d)\n", iss->timing.stall_cycles_get());

    if (likely(iss->timing.stall_cycles_get() == 0))
    {
        // Takes care first of all optional features (traces, VCD and so on)
        _this->insn_exec_profiling();

        iss_insn_t *insn = _this->current_insn;

        // Execute the instruction and replace the current one with the new one
        _this->current_insn = _this->insn_exec_fast(insn);
        _this->prev_insn = insn;

        // Now that we have the new instruction, we can fetch it. In case the response is asynchronous,
        // this will stall the ISS, which will execute the next instruction when the response is
        // received
        iss->prefetcher.fetch(_this->current_insn);

        // Since power instruction information is filled when the instruction is decoded,
        // make sure we account it only after the instruction is executed
        _this->insn_exec_power(insn);
    }
    else
    {
        iss->timing.stall_cycles_dec();
    }
}

void Exec::exec_instr_check_all(void *__this, vp::clock_event *event)
{
    Exec *_this = (Exec *)__this;

    _this->trace.msg(vp::trace::LEVEL_TRACE, "Handling instruction with slow handler\n");

    if (likely(_this->iss.timing.stall_cycles_get() == 0))
    {
        // Switch back to optimize instruction handler only
        // if HW counters are disabled as they are checked with the slow handler
        if (_this->can_switch_to_fast_mode())
        {
            _this->instr_event->meth_set(_this, &Exec::exec_instr);
        }

        _this->insn_exec_profiling();

        int cycles;

        iss_insn_t *insn = _this->current_insn;

        // Don't execute the instruction if an IRQ was taken and it triggered a pending fetch
        if (!_this->iss.irq.check() && !_this->stalled.get())
        {
            iss_insn_t *insn = _this->current_insn;
            _this->current_insn = _this->insn_exec(insn);
            _this->prev_insn = insn;

            _this->iss.prefetcher.fetch(_this->current_insn);

            _this->iss.timing.insn_account();
        }

        _this->insn_exec_power(insn);

        _this->dbg_unit_step_check();
    }
    else
    {
        _this->iss.timing.stall_cycles_dec();
    }
}

void Exec::exec_first_instr(vp::clock_event *event)
{
    this->instr_event->meth_set(this, &Exec::exec_instr);
    this->exec_instr((void *)this, event);
}

void Exec::exec_first_instr(void *__this, vp::clock_event *event)
{
    Exec *_this = (Exec *)__this;
    _this->exec_first_instr(event);
}

void Exec::clock_sync(void *__this, bool active)
{
    Exec *_this = (Exec *)__this;
    _this->trace.msg("Setting clock (active: %d)\n", active);

    _this->clock_active = active;
}

void Exec::fetchen_sync(void *__this, bool active)
{
    Exec *_this = (Exec *)__this;
    _this->trace.msg("Setting fetch enable (active: %d)\n", active);
    int old_val = _this->fetch_enable_reg.get();
    _this->fetch_enable_reg.set(active);
    if (!old_val && active)
    {
        _this->stalled_dec();
        _this->pc_set(_this->bootaddr_reg.get() + _this->bootaddr_offset);
    }
    else if (old_val && !active)
    {
        // In case of a falling edge, stall the core to prevent him from executing
        _this->stalled_inc();
    }
}

void Exec::bootaddr_sync(void *__this, uint32_t value)
{
    Exec *_this = (Exec *)__this;
    _this->trace.msg("Setting boot address (value: 0x%x)\n", value);
    _this->bootaddr_reg.set(value);
    _this->iss.irq.vector_table_set(_this->bootaddr_reg.get() & ~((1 << 8) - 1));
}



void Exec::pc_set(iss_addr_t value)
{
    this->current_insn = insn_cache_get(&this->iss, value);

    // Since the ISS needs to fetch the instruction in advanced, we force the core
    // to refetch the current instruction
    this->iss.prefetcher.fetch(this->current_insn);
}

void Exec::flush_cache_ack_sync(void *__this, bool active)
{
    Exec *_this = (Exec *)__this;
    if (_this->iss.state.cache_sync)
    {
        _this->iss.state.cache_sync = false;
        _this->stalled_dec();
        _this->insn_terminate();
    }
}
