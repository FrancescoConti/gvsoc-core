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

#ifndef __CPU_ISS_RV32M_HPP
#define __CPU_ISS_RV32M_HPP

static inline iss_insn_t *mul_exec(Iss *iss, iss_insn_t *insn)
{
    REG_SET(0, LIB_CALL2(lib_MULU, REG_GET(0), REG_GET(1)));
    return insn->next;
}

static inline iss_insn_t *mulh_exec(Iss *iss, iss_insn_t *insn)
{
    REG_SET(0, ((iss_lsim_t)(iss_sim_t)REG_GET(0) * (iss_lsim_t)(iss_sim_t)REG_GET(1)) >> ISS_REG_WIDTH);
    return insn->next;
}

static inline iss_insn_t *mulhsu_exec(Iss *iss, iss_insn_t *insn)
{
    REG_SET(0, ((iss_lsim_t)(iss_sim_t)REG_GET(0) * (iss_uim_t)REG_GET(1)) >> ISS_REG_WIDTH);
    return insn->next;
}

static inline iss_insn_t *mulhu_exec(Iss *iss, iss_insn_t *insn)
{
    REG_SET(0, ((iss_luim_t)REG_GET(0) * (iss_luim_t)REG_GET(1)) >> ISS_REG_WIDTH);
    return insn->next;
}

static inline iss_insn_t *div_exec(Iss *iss, iss_insn_t *insn)
{
    iss_sim_t divider = REG_GET(1);
    iss_sim_t dividend = REG_GET(0);
    iss_sim_t result;
    if (divider == 0)
        result = -1;
    else if (divider == (1ULL << (ISS_REG_WIDTH - 1)) && dividend == -1)
        result = 0;
    else if (dividend == (1ULL << (ISS_REG_WIDTH - 1)) && divider == -1)
        result = (1ULL << (ISS_REG_WIDTH - 1));
    else
        result = dividend / divider;
    REG_SET(0, result);

    int cycles;

    if (divider == 0)
    {
        cycles = 1;
    }
    else if (divider > 0)
    {
        cycles = __builtin_clz(divider) + 3;
    }
    else
    {
        cycles = __builtin_clz((~divider) + 1) + 2;
    }

    iss->timing.stall_insn_dependency_account(cycles);

    return insn->next;
}

static inline iss_insn_t *divu_exec(Iss *iss, iss_insn_t *insn)
{
    iss_uim_t divider = REG_GET(1);
    iss_uim_t dividend = REG_GET(0);
    iss_uim_t result;
    if (divider == 0)
        result = -1;
    else
        result = dividend / divider;
    REG_SET(0, result);

    iss->timing.stall_insn_dependency_account(__builtin_clz(divider) + 3);

    return insn->next;
}

static inline iss_insn_t *rem_exec(Iss *iss, iss_insn_t *insn)
{
    iss_sim_t divider = REG_GET(1);
    iss_sim_t dividend = REG_GET(0);
    iss_sim_t result;
    if (divider == 0)
        result = dividend;
    else if (divider == (1ULL << (ISS_REG_WIDTH - 1)) && dividend == -1)
        result = -1;
    else if (dividend == (1ULL << (ISS_REG_WIDTH - 1)) && divider == -1)
        result = 0;
    else
        result = dividend % divider;
    REG_SET(0, result);

    int cycles;

    if (divider == 0)
    {
        cycles = 1;
    }
    else if (divider > 0)
    {
        cycles = __builtin_clz(divider) + 3;
    }
    else
    {
        cycles = __builtin_clz((~divider) + 1) + 2;
    }

    iss->timing.stall_insn_dependency_account(cycles);

    return insn->next;
}

static inline iss_insn_t *remu_exec(Iss *iss, iss_insn_t *insn)
{
    iss_uim_t divider = REG_GET(1);
    iss_uim_t dividend = REG_GET(0);
    iss_uim_t result;
    if (divider == 0)
        result = dividend;
    else
        result = dividend % divider;

    REG_SET(0, result);

    iss->timing.stall_insn_dependency_account(__builtin_clz(divider) + 3);

    return insn->next;
}

#endif
