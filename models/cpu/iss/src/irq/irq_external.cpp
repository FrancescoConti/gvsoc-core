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
#include <irq/irq_external_implem.hpp>
#include <iss.hpp>

Irq::Irq(Iss &iss)
    : iss(iss)
{
}

void Irq::build()
{
    for (int i = 0; i < 32; i++)
    {
        this->vectors[i] = NULL;
    }
    iss.traces.new_trace("irq", &this->trace, vp::DEBUG);
}

iss_insn_t *Irq::mret_handle()
{
    this->iss.exec.switch_to_full_mode();
    this->iss.irq.irq_enable = this->iss.irq.saved_irq_enable;
    this->iss.csr.mcause = 0;

    return insn_cache_get(&this->iss, this->iss.csr.epc);
}

iss_insn_t *Irq::dret_handle()
{
    this->iss.exec.switch_to_full_mode();
    this->iss.irq.irq_enable = this->iss.irq.debug_saved_irq_enable;
    this->iss.state.debug_mode = 0;

    return insn_cache_get(&this->iss, this->iss.csr.depc);
}

void Irq::vector_table_set(iss_addr_t base)
{
    this->trace.msg("Setting vector table (addr: 0x%x)\n", base);
    for (int i = 0; i < 32; i++)
    {
        this->iss.irq.vectors[i] = insn_cache_get(&this->iss, base + i * 4);
    }

    for (int i = 32; i < 35; i++)
    {
        this->iss.irq.vectors[i] = insn_cache_get(&this->iss, base + i * 4);
    }
    this->iss.irq.vector_base = base;
}

void Irq::cache_flush()
{
    this->vector_table_set(this->iss.irq.vector_base);
    this->iss.irq.debug_handler = insn_cache_get(&this->iss, this->iss.config.debug_handler);
}

void Irq::reset(bool active)
{
    this->iss.state.elw_interrupted = 0;
    this->vector_base = 0;
    this->irq_enable = 0;
    this->saved_irq_enable = 0;
    this->req_irq = -1;
    this->req_debug = false;
    this->debug_handler = insn_cache_get(&this->iss, this->iss.config.debug_handler);
}

void Irq::wfi_handle()
{
    // The instruction loop is checking for IRQs only if interrupts are globally enable
    // while wfi ends as soon as one interrupt is active even if interrupts are globally disabled,
    // so we have to check now if we can really go to sleep.
    if (this->req_irq == -1)
    {
        this->iss.exec.wfi.set(true);
        this->iss.exec.insn_stall();
    }
}

void Irq::elw_irq_unstall()
{
    this->trace.msg(vp::trace::LEVEL_TRACE, "%s %d\n", __FILE__, __LINE__);

    this->trace.msg("Interrupting pending elw\n");
    this->iss.exec.current_insn = this->iss.state.elw_insn;
    // Keep the information that we interrupted it, so that features like HW loop
    // knows that the instruction is being replayed
    this->iss.state.elw_interrupted = 1;
}

void Irq::irq_req_sync(void *__this, int irq)
{
    Irq *_this = (Irq *)__this;

    _this->trace.msg(vp::trace::LEVEL_TRACE, "Received IRQ (irq: %d)\n", irq);

    _this->req_irq = irq;

    if (irq != -1 && _this->iss.exec.wfi.get())
    {
        _this->iss.exec.wfi.set(false);
        _this->iss.exec.stalled_dec();
        _this->iss.exec.insn_terminate();
    }

    if (_this->iss.elw_stalled.get() && irq != -1 && _this->irq_enable)
    {
        _this->elw_irq_unstall();
    }

    _this->iss.exec.switch_to_full_mode();
}

int Irq::check()
{
    if (this->req_debug && !this->iss.state.debug_mode)
    {
        this->iss.state.debug_mode = true;
        this->iss.csr.depc = this->iss.exec.current_insn->addr;
        this->debug_saved_irq_enable = this->irq_enable;
        this->irq_enable = 0;
        this->req_debug = false;
        this->iss.exec.current_insn = this->debug_handler;
        return 1;
    }
    else
    {
        int req_irq = this->req_irq;
        if (req_irq != -1 && this->irq_enable)
        {
            this->trace.msg(vp::trace::LEVEL_TRACE, "Handling IRQ (irq: %d)\n", req_irq);

            this->iss.csr.epc = this->iss.exec.current_insn->addr;
            this->saved_irq_enable = this->irq_enable;
            this->irq_enable = 0;
            this->req_irq = -1;
            this->iss.exec.current_insn = this->vectors[req_irq];
            this->iss.csr.mcause = (1 << 31) | (unsigned int)req_irq;

            this->trace.msg("Acknowledging interrupt (irq: %d)\n", req_irq);
            this->iss.irq_ack_itf.sync(req_irq);

            this->iss.timing.stall_insn_dependency_account(4);

            this->iss.prefetcher.fetch(this->iss.exec.current_insn);

            return 1;
        }
    }

    return 0;
}
