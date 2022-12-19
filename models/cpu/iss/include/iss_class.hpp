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

#pragma once

#include <lsu.hpp>
#include <decode.hpp>
#include <trace.hpp>
#include <csr.hpp>
#include <dbgunit.hpp>
#include <syscalls.hpp>
#include <timing.hpp>
#include <regfile.hpp>
#include <irq/irq_external.hpp>
#include <exec/exec_inorder.hpp>
#include <prefetch/prefetch_single_line.hpp>
#include <gdbserver.hpp>

class Iss : public vp::component
{

public:
    Iss(js::config *config);

    int build();
    void start();
    void pre_reset();
    void reset(bool active);
    int iss_open();
    virtual void target_open();

    Prefetcher prefetcher;
    Exec exec;
    Decode decode;
    Timing timing;
    Irq irq;
    Gdbserver gdbserver;
    Lsu lsu;
    DbgUnit dbgunit;
    Syscalls syscalls;
    Trace trace;
    Csr csr;
    Regfile regfile;


    // wrapper
    vp::trace wrapper_trace;
    bool iss_opened;
    iss_config_t config;
    iss_pulpv2_t pulpv2;
    iss_pulp_nn_t pulp_nn;
    iss_rnnext_t rnnext;
    iss_corev_t corev;




};
