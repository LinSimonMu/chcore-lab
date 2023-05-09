#include <common/util.h>
#include <common/macro.h>
#include <common/kprint.h>

#include "buddy.h"

int __get_page_index(struct phys_mem_pool * pool, struct page *page);
int __min(int x, int y);

/*
 * The layout of a phys_mem_pool:
 * | page_metadata are (an array of struct page) | alignment pad | usable memory |
 *
 * The usable memory: [pool_start_addr, pool_start_addr + pool_mem_size).
 */
void init_buddy(struct phys_mem_pool *pool, struct page *start_page,
        vaddr_t start_addr, u64 page_num)
{
    int order;
    int page_idx;
    struct page *page;

    /* Init the physical memory pool. */
    pool->pool_start_addr = start_addr;
    pool->page_metadata = start_page;
    pool->pool_mem_size = page_num * BUDDY_PAGE_SIZE;
    /* This field is for unit test only. */
    pool->pool_phys_page_num = page_num;

    /* Init the free lists */
    for (order = 0; order < BUDDY_MAX_ORDER; ++order) {
        pool->free_lists[order].nr_free = 0;
        init_list_head(&(pool->free_lists[order].free_list));
    }

    /* Clear the page_metadata area. */
    memset((char *)start_page, 0, page_num * sizeof(struct page));

    /* Init the page_metadata area. */
    for (page_idx = 0; page_idx < page_num; ++page_idx) {
        page = start_page + page_idx;
        page->allocated = 1;
        page->order = 0;
    }

    /* Put each physical memory page into the free lists. */
    for (page_idx = 0; page_idx < page_num; ++page_idx) {
        page = start_page + page_idx;
        // skip the page without order 0, id not 0, means that has been meraged
        if(page->order != 0) {
            //continue;
        }
        buddy_free_pages(pool, page);
    }
}

static struct page *get_buddy_chunk(struct phys_mem_pool *pool,
                    struct page *chunk)
{
    u64 chunk_addr;
    u64 buddy_chunk_addr;
    int order;

    /* Get the address of the chunk. */
    chunk_addr = (u64) page_to_virt(pool, chunk);
    order = chunk->order;
    /*
     * Calculate the address of the buddy chunk according to the address
     * relationship between buddies.
     */
#define BUDDY_PAGE_SIZE_ORDER (12)
    buddy_chunk_addr = chunk_addr ^
        (1UL << (order + BUDDY_PAGE_SIZE_ORDER));

    /* Check whether the buddy_chunk_addr belongs to pool. */
    if ((buddy_chunk_addr < pool->pool_start_addr) ||
        (buddy_chunk_addr >= (pool->pool_start_addr +
                  pool->pool_mem_size))) {
        return NULL;
    }

    return virt_to_page(pool, (void *)buddy_chunk_addr);
}

/*
 * split_page: split the memory block into two smaller sub-block, whose order
 * is half of the origin page.
 * pool @ physical memory structure reserved in the kernel
 * order @ order for origin page block
 * page @ splitted page
 * 
 * Hints: don't forget to substract the free page number for the corresponding free_list.
 * you can invoke split_page recursively until the given page can not be splitted into two
 * smaller sub-pages.
 */
static struct page *split_page(struct phys_mem_pool *pool, u64 order,
                   struct page *page)
{
    // <lab2>
    struct page *split_page = NULL;
    // when the free page order is bigger than current order request
    // split the buddy page
    int high_order = page->order;
    u64 size = 1 << high_order;
    while(high_order > order) {
        high_order--;
        size >>= 1;
        list_add(&((page + size)->node), &(pool->free_lists[high_order]));
        pool->free_lists[high_order].nr_free++;
        //set all page new order
        int page_index = __get_page_index(pool, page);
        for(int i = page_index; i < page_index + (1 << (high_order + 1)); i++) {
            (pool->page_metadata + i)->allocated = 0;
            (pool->page_metadata + i)->order = high_order;
        }
        split_page = page;
    }
    return split_page;
    // </lab2>
}

/*
 * buddy_get_pages: get free page from buddy system.
 * pool @ physical memory structure reserved in the kernel
 * order @ get the (1<<order) continous pages from the buddy system
 * 
 * Hints: Find the corresonding free_list which can allocate 1<<order
 * continuous pages and don't forget to split the list node after allocation   
 */
struct page *buddy_get_pages(struct phys_mem_pool *pool, u64 order)
{
    // <lab2>
    struct page *page = NULL;

    // find freelist with the order, if free pages satisfy need,
    // (1) return the first page

    u64 curr_order;
    for(curr_order = order; curr_order < BUDDY_MAX_ORDER; curr_order++) {
        if(pool->free_lists[curr_order].nr_free == 0) {
            continue;
        }
        page = list_entry(pool->free_lists[curr_order].free_list.next, struct page, node);
        list_del(&(page->node));
        pool->free_lists[curr_order].nr_free--;
        split_page(pool, order, page);
        page->allocated = 1;
        return page;
    }
    return page;
    // </lab2>
}

/*
 * merge_page: merge the given page with the buddy page
 * pool @ physical memory structure reserved in the kernel
 * page @ merged page (attempted)
 * 
 * Hints: you can invoke the merge_page recursively until
 * there is not corresponding buddy page. get_buddy_chunk
 * is helpful in this function.
 */
static struct page *merge_page(struct phys_mem_pool *pool, struct page *page)
{
    // <lab2>
    int page_index = 0;
    int buddy_index = 0;
    int combine_index = 0;
    struct page *buddy_page = NULL;
    struct page *mg_page = NULL;

    int order = page->order;
    mg_page = page;
    page_index = __get_page_index(pool, mg_page);

    while(order < BUDDY_MAX_ORDER - 1) {    
        buddy_page = get_buddy_chunk(pool, mg_page);
        buddy_index = __get_page_index(pool, buddy_page);
        bool is_buddy_page = (buddy_page->order == order && buddy_page->allocated == 0);
        if(!is_buddy_page) {
            break;
        }
        // find buddy page
        pool->free_lists[order].nr_free--;
        combine_index = __min(page_index, buddy_index);
        mg_page = mg_page + (combine_index - page_index);
        page_index = combine_index;
        order++;
        mg_page->order = order;
    }
    // set page order
    mg_page->order = order;
    for(int i = page_index; i < page_index + (1<<order); i++) {
        (pool->page_metadata + i)->allocated = 0;
        (pool->page_metadata + i)->order = order;
    }

    list_add(&(mg_page->node), &(pool->free_lists[order].free_list));
    pool->free_lists[order].nr_free++;

    return mg_page;
    // </lab2>
}

/*
 * buddy_free_pages: give back the pages to buddy system
 * pool @ physical memory structure reserved in the kernel
 * page @ free page structure
 * 
 * Hints: you can invoke merge_page.
 */
void buddy_free_pages(struct phys_mem_pool *pool, struct page *page)
{
    // <lab2>
    if(page->allocated == 1) {
        page->allocated = 0;
        merge_page(pool, page);
    }

    // </lab2>
}

void *page_to_virt(struct phys_mem_pool *pool, struct page *page)
{
    u64 addr;

    /* page_idx * BUDDY_PAGE_SIZE + start_addr */
    addr = (page - pool->page_metadata) * BUDDY_PAGE_SIZE +
        pool->pool_start_addr;
    return (void *)addr;
}

struct page *virt_to_page(struct phys_mem_pool *pool, void *addr)
{
    struct page *page;

    page = pool->page_metadata +
        (((u64) addr - pool->pool_start_addr) / BUDDY_PAGE_SIZE);
    return page;
}

u64 get_free_mem_size_from_buddy(struct phys_mem_pool * pool)
{
    int order;
    struct free_list *list;
    u64 current_order_size;
    u64 total_size = 0;

    for (order = 0; order < BUDDY_MAX_ORDER; order++) {
        /* 2^order * 4K */
        current_order_size = BUDDY_PAGE_SIZE * (1 << order);
        list = pool->free_lists + order;
        total_size += list->nr_free * current_order_size;

        /* debug : print info about current order */
        kdebug("buddy memory chunk order: %d, size: 0x%lx, num: %d\n",
               order, current_order_size, list->nr_free);
    }
    return total_size;
}

int __get_page_index(struct phys_mem_pool * pool, struct page *page) {
    if(page) {
        return (page - pool->page_metadata);
    }
    else {
        return -1;
    }
}

int __min(int x, int y) {
    return (x < y ? x : y);
}