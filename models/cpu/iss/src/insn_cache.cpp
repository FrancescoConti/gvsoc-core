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

#include "cpu/iss/include/iss.hpp"
#include <string.h>

static void insn_block_init(Iss *iss, iss_insn_block_t *b, iss_addr_t pc);

static void flush_cache(Iss *iss, iss_insn_cache_t *cache)
{
    iss->prefetcher.flush();

    for (auto page: cache->pages)
    {
        delete page.second;
    }

    cache->pages.clear();

    iss_cache_vflush(iss);

    for (auto insn_table: iss->decode.insn_tables)
    {
        delete[] insn_table;
    }

    iss->decode.insn_tables.clear();
}

int insn_cache_init(Iss *iss)
{
    iss_insn_cache_t *cache = &iss->decode.insn_cache;
    cache->current_insn_page = NULL;
    return 0;
}

bool insn_cache_is_decoded(Iss *iss, iss_insn_t *insn)
{
    return insn->handler != iss_decode_pc_handler;
}



void iss_cache_flush(Iss *iss)
{
    flush_cache(iss, &iss->decode.insn_cache);

    iss->gdbserver.enable_all_breakpoints();

    iss->irq.cache_flush();
}

void iss_cache_vflush(Iss *iss)
{
    iss_insn_cache_t *cache = &iss->decode.insn_cache;
    cache->current_insn_page = NULL;
#ifdef CONFIG_GVSOC_ISS_UNTIMED_LOOP
    // Since the untimed loop is trying to directly get instruction from page,
    // we need to stop it when current page is changed
    iss->exec.loop_count = 0;
#endif
}



void Decode::flush_cache_sync(vp::Block *__this, bool active)
{
    Decode *_this = (Decode *)__this;
    iss_cache_flush(&_this->iss);
}



iss_insn_page_t *insn_cache_page_get(Iss *iss, iss_reg_t paddr)
{
    iss_insn_cache_t *cache = &iss->decode.insn_cache;
    iss_reg_t index = paddr >> INSN_PAGE_BITS;
    iss_insn_page_t *page = cache->pages[index];
    if (page != NULL)
    {
        return page;
    }

    page = new iss_insn_page_t;

    cache->pages[index] = page;

    iss_reg_t addr = index << INSN_PAGE_BITS;
    for (int i=0; i<INSN_PAGE_SIZE; i++)
    {
        insn_init(&page->insns[i], addr);
        addr += 2;
    }

    return page;
}



iss_insn_t *insn_cache_get_insn_from_cache(Iss *iss, iss_reg_t vaddr, iss_reg_t &index)
{
    iss_reg_t paddr;
    iss_insn_cache_t *cache = &iss->decode.insn_cache;

#ifdef CONFIG_GVSOC_ISS_MMU
    if (iss->mmu.insn_virt_to_phys(vaddr, paddr))
    {
        return NULL;
    }
#else
    paddr = vaddr;
#endif

    cache->current_insn_page = insn_cache_page_get(iss, paddr);
    cache->current_insn_page_base = (vaddr >> INSN_PAGE_BITS) << INSN_PAGE_BITS;

    return insn_cache_get_insn(iss, vaddr, index);
}