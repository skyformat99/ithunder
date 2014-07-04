#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include "lmap.h"
#include "rwlock.h"

LMAP *lmap_init(char *file)
{
    LMAP *lmap = NULL;
    char path[1024];
    struct stat st = {0};
    off_t size = 0;
    int i = 0;

    if(file && (lmap = (LMAP *)calloc(1, sizeof(LMAP))))
    {
       if((lmap->fd = open(file, O_CREAT|O_RDWR, 0644)) > 0) 
       {
           size = lmap->msize = (off_t)sizeof(LMMSTATE) + (off_t)sizeof(LMMKV) * (off_t)LMM_NODES_MAX;
           lmap->state = (LMMSTATE*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, lmap->fd, 0);
           lmap->map = (LMMKV *)((char *)lmap->state + sizeof(LMMSTATE));
           fstat(lmap->fd, &st);
           lmap->size = st.st_size;
           if(st.st_size < sizeof(LMMSTATE))
           {
                i = ftruncate(lmap->fd, sizeof(LMMSTATE));
                memset(lmap->state, 0, sizeof(LMMSTATE));
                lmap->size += sizeof(LMMSTATE);
                for(i = 0; i < LMM_SLOT_MAX; i++)
                {
                   lmap->state->slots[i].nodeid = -1;
                }
           }
           lmap->roots = lmap->state->roots;
           lmap->slots = lmap->state->slots;
           //fprintf(stdout, "size:%lld/%d\n", lmap->size, sizeof(LMMSTATE));
       }
       else
       {
           fprintf(stderr, "open %s failed, %s\n", file, strerror(errno));
       }
       sprintf(path, "%s.v", file);
       if((lmap->vfd = open(path, O_CREAT|O_RDWR, 0644)) > 0) 
       {
            size = lmap->vmsize = (off_t)sizeof(LMMV) *  (off_t)LMM_NODES_MAX;
            lmap->vmap = (LMMV *)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, lmap->vfd, 0);
            fstat(lmap->vfd, &st);
            lmap->vsize = st.st_size;
            //fprintf(stdout, "vsize:%lld\n", lmap->vsize);
       }
       else
       {
           fprintf(stderr, "open %s failed, %s\n", file, strerror(errno));
       }
       RWLOCK_INIT(lmap->rwlock);
    }
    return lmap;
}

/* lmap set val */
int lmap_vset(LMAP *lmap, u32_t no, int64_t val)
{
    off_t size = (off_t)((no / LMM_LMMV_INC) + 1) 
            * (off_t)LMM_LMMV_INC * (off_t) sizeof(LMMV);
    int ret = -1, n = 0, i = 0;

    if(lmap && lmap->state && no >= 0 && no < LMM_NODES_MAX)
    {
        if(size > lmap->vsize)
        {
            n = ftruncate(lmap->vfd, size);
            //memset(((char *)lmap->vmap+lmap->vsize), 0, size - lmap->vsize);
            i = lmap->vsize / sizeof(LMMV);
            n = size / sizeof(LMMV);
            while(i < n) lmap->vmap[i++].off = -1;
            lmap->vsize = size;
        }
        //lmap->vmap[no].val = val; 
        ret = 0;
    }
    return ret;
}

/* lmap get val */
int lmap_vget(LMAP *lmap, u32_t no, int64_t *val)
{
    int ret = -1, n = 0;

    if((n = (lmap->vsize/sizeof(LMMV))) > 0 && no >= 0 && no < n)
    {
        if(val) *val = lmap->vmap[no].val; 
        ret = 0;
    }
    return ret;
}

/* new bolt  */
int lmap_slot_new(LMAP *lmap)
{
    off_t size = 0;
    int ret = -1, n = 0;

    if(lmap && lmap->state)
    {
        if(lmap->state->nleft > 0)
        {
            n = --(lmap->state->nleft);
            ret = lmap->state->qleft[n];
        }
        else
        {
            size = lmap->size + (off_t)sizeof(LMMKV) * (off_t)LMM_SLOT_NUM; 
            n = ftruncate(lmap->fd, size);
            memset(((char *)lmap->state+lmap->size), 0, (size - lmap->size));
            ret = (lmap->size - (off_t)sizeof(LMMSTATE)) / (off_t)sizeof(LMMKV);
            lmap->size = size;
        }
    }
    return ret;
}

int lmap_insert(LMAP *lmap, u32_t no, int64_t key)
{
    int ret = -1, i = 0, k = -1, nodeid = 0, pos = 0, num = 0, 
        n = 0, x = 0, min = 0, max = 0;
    LMMKV *kvs = NULL, *kv = NULL;
    LMMV *vnodes = NULL;

    if(lmap && lmap->state && (vnodes = lmap->vmap))
    {
        if((n = lmap->state->count) > 0)
        {
            max = n - 1;
            min = 0;
            if(key <= lmap->slots[min].max) k = min;
            else if(key >= lmap->slots[max].min) k = max;
            else
            {
                while(max > min)
                {
                    x = (min + max) / 2;
                    if(x == min)
                    {
                        k = x;
                        break;
                    }
                    if(key >=  lmap->slots[x].min && key <= lmap->slots[x+1].min)
                    {
                        k = x;
                        break;
                    }
                    else if(key > lmap->slots[x].max) min = x;
                    else max = x;
                }
            }
        }
        /* 未满的slot 直接插入 */
        if(k >= 0 && k < n && lmap->slots[k].count < LMM_SLOT_NUM)
        {
            x = lmap->slots[k].count++;
            kvs = lmap->map + lmap->slots[k].nodeid;
            while(x > 0 && key < kvs[x-1].key)
            {
                kvs[x].key = kvs[x-1].key;
                kvs[x].val = kvs[x-1].val;
                lmap->vmap[(kvs[x].val)].off = (lmap->slots[k].nodeid + x);
                --x;
            }
            kvs[x].key = key;
            kvs[x].val = no;
            lmap->vmap[no].off = (lmap->slots[k].nodeid + x);
            if(key < lmap->slots[k].min) lmap->slots[k].min = key;
            if(key > lmap->slots[k].max) lmap->slots[k].max = key;
        }
        else
        {
            nodeid = lmap_slot_new(lmap);
            /* slot已满 转移元素到新的slot */
            if(k >= 0 && k < n && lmap->slots[k].count == LMM_SLOT_NUM)
            {
                kvs = lmap->map + lmap->slots[k].nodeid + LMM_SLOT2_NUM;
                kv = lmap->map + nodeid;
                num = LMM_SLOT2_NUM;
                x = 0;
                for(i = 0; i < LMM_SLOT2_NUM; i++) 
                {
                    if(key >= kvs->key && key < kvs[i].key && num == LMM_SLOT2_NUM) 
                    {
                        kv->key = key;
                        kv->val = no;
                        ++kv;
                        ++x;
                        lmap->vmap[no].off = (nodeid + x);
                        num = LMM_SLOT2_NUM+1;
                    }
                    kv->key = kvs[i].key;
                    kv->val = kvs[i].val;
                    ++kv;
                    ++x;
                    lmap->vmap[(kv->val)].off = (nodeid + x);
                }
                if(num == LMM_SLOT2_NUM && key >= kvs[i-1].key)
                {
                    kv->key = key;
                    kv->val = no;
                    ++kv;
                    ++x;
                    num = LMM_SLOT2_NUM+1;
                    lmap->vmap[(kv->val)].off = (nodeid + x);
                }
                if(num == LMM_SLOT2_NUM)
                {
                    kvs = lmap->map + lmap->slots[k].nodeid;
                    i = LMM_SLOT2_NUM;
                    while(i > 0 && key < kvs[i-1].key)
                    {
                        kvs[i].key = kvs[i-1].key;
                        kvs[i].val = kvs[i-1].val;
                        lmap->vmap[(kvs[i].val)].off = (lmap->slots[k].nodeid + i);
                        --i;
                    }
                    kvs[i].key = key;
                    kvs[i].val = no;
                    lmap->vmap[no].off = (lmap->slots[k].nodeid + i);
                    lmap->slots[k].min = kvs[0].key;
                    lmap->slots[k].max = kvs[LMM_SLOT2_NUM].key;
                    lmap->slots[k].count = LMM_SLOT2_NUM + 1;
                }
                else
                {
                    kvs = lmap->map + lmap->slots[k].nodeid;
                    lmap->slots[k].min = kvs[0].key;
                    lmap->slots[k].max = kvs[LMM_SLOT2_NUM-1].key;
                    lmap->slots[k].count = LMM_SLOT2_NUM;
                }
                pos = k+1;
                k = lmap->state->count++; 
                while(k > pos)
                {
                    memcpy(&(lmap->slots[k]), &(lmap->slots[k-1]), sizeof(LMMSLOT));
                    x = (lmap->slots[k].nodeid / LMM_SLOT_NUM);
                    lmap->roots[x] = k;
                    --k;
                }
            }
            else
            {
                /* 创建新的slot */
                kv = lmap->map + nodeid;
                kv->key = key;
                kv->val = no;
                lmap->vmap[no].off = nodeid;
                num = 1;
                k = lmap->state->count++; 
                while(k > 0 && key <= lmap->slots[k].min)
                {
                    memcpy(&(lmap->slots[k]), &(lmap->slots[k-1]), sizeof(LMMSLOT));
                    x = (lmap->slots[k].nodeid / LMM_SLOT_NUM);
                    lmap->roots[x] = k;
                    --k;
                }
            }
            kv = lmap->map + nodeid;
            lmap->slots[k].min = kv[0].key;
            lmap->slots[k].max = kv[num-1].key;
            lmap->slots[k].nodeid = nodeid;
            lmap->slots[k].count = num;
            //fprintf(stdout, "%s::%d k:%d min:%d max:%d num:%d/%d\n", __FILE__, __LINE__, k, lmap->slots[k].min, lmap->slots[k].max, num, lmap->state->count);
        }
    }
    return ret;
}

int lmap_remove(LMAP *lmap, u32_t no)
{
    u32_t nodeid = 0, rootid = 0, slotid = 0;
    int ret = -1, i = 0, x = 0, n = 0;
    LMMKV *kvs = NULL;

    if(lmap && lmap->state && lmap->vmap 
            && (n = lmap->vsize/sizeof(LMMV)) > 0 && no >= 0 && no < n
            && (nodeid = lmap->vmap[no].off) >= 0)
    {
        rootid = (nodeid / LMM_SLOT_NUM);
        slotid = lmap->roots[rootid];
        i = nodeid % LMM_SLOT_NUM;
        kvs = lmap->map + lmap->slots[slotid].nodeid;    
        while(i < lmap->slots[slotid].count)
        {
            memcpy(&(kvs[i]), &(kvs[i+1]), sizeof(LMMKV));
            x = kvs[i].val;
            lmap->vmap[x].off = i;
            ++i;
        }
        if(lmap->slots[slotid].count == 1)
        {
            i = slotid;
            lmap->state->qleft[(lmap->state->nleft++)] = nodeid;
            lmap->roots[rootid] = -1;
            while(i < lmap->state->count)
            {
                memcpy(&(lmap->slots[i]), &(lmap->slots[i+1]), sizeof(LMMSLOT));
                rootid = (lmap->slots[i].nodeid / LMM_SLOT_NUM);
                lmap->roots[rootid] = i;
                ++i;
            }
            --lmap->state->count;
        }
        else
        {
            n = --(lmap->slots[slotid].count);
            lmap->slots[slotid].min = kvs[0].key;
            lmap->slots[slotid].max = kvs[n - 1].key;
        }
    }
    return ret;
}

int lmap_find_slot(LMAP *lmap, int64_t key)
{
    int n = 0, min = 0, max = 0, x = 0, ret = -1;
    if(lmap && (n = lmap->state->count) > 0)
    {
        max = n - 1;
        min = 0;
        if(key <= lmap->slots[min].max) ret = min;
        else if(key >= lmap->slots[max].min) ret = max;
        else
        {
            while(max > min)
            {
                x = (min + max) / 2;
                if(x == min)
                {
                    ret = x;
                    break;
                }
                if(key >=  lmap->slots[x].min && key <= lmap->slots[x+1].min)
                {
                    ret = x;
                    break;
                }
                else if(key > lmap->slots[x].max) min = x;
                else max = x;
            }
        }
        if((x = ret)>= 0 && x < n)
        {
            while(x >= 0 && key <= lmap->slots[x].max)
            {
                ret = x--;
            }
        }
    }
    return ret;
}

int lmap_find_slot2(LMAP *lmap, int64_t key)
{
    int n = 0, min = 0, max = 0, x = 0, ret = -1;
    if(lmap && (n = lmap->state->count) > 0)
    {
        max = n - 1;
        min = 0;
        if(key <= lmap->slots[min].max) ret = min;
        else if(key >= lmap->slots[max].min) ret = max;
        else
        {
            while(max > min)
            {
                x = (min + max) / 2;
                if(x == min)
                {
                    ret = x;
                    break;
                }
                if(key >=  lmap->slots[x].min && key <= lmap->slots[x+1].min)
                {
                    ret = x;
                    break;
                }
                else if(key > lmap->slots[x].max) min = x;
                else max = x;
            }
        }
        if((x = ret)>= 0 && x < n)
        {
            while(x < n && key >= lmap->slots[x].min) 
            {
                ret = x++;
            }
        }
    }
    return ret;
}

int lmap_find_kv(LMAP *lmap, int k, int64_t key)
{
    int n = 0, min = 0, max = 0, x = 0, ret = -1;
    LMMKV *kvs = NULL;

    if(lmap && k >= 0 && k < lmap->state->count 
            && (n = lmap->slots[k].count) > 0)
    {
        kvs = lmap->map + lmap->slots[k].nodeid;
        min = 0;
        max = n - 1; 
        if(key <= kvs[min].key) ret = min;
        else
        {
            while(max > min)
            {
                x = (min + max) / 2;
                if(x == min)
                {
                    ret = x;
                    break;
                }
                if(key ==  kvs[x].key)
                {
                    ret = x;
                    break;
                }
                else if(key > kvs[x].key) min = x;
                else max = x;
            }
            if((x = ret) >= 0 && x < n)
            {
                while(x >= 0 && key == kvs[x].key)
                {
                    ret = x--;
                }
            }

        }
        //fprintf(stdout, "find_kv(%d) min:%d max:%d count:%d ret:%d\n", key, lmap->slots[k].min, lmap->slots[k].max, lmap->slots[k].count, ret);
    }
    return ret;
}

int lmap_find_kv2(LMAP *lmap, int k, int64_t key)
{
    int n = 0, min = 0, max = 0, x = 0, ret = -1;
    LMMKV *kvs = NULL;

    if(lmap && k >= 0 && k < lmap->state->count 
            && (n = lmap->slots[k].count) > 0)
    {
        kvs = lmap->map + lmap->slots[k].nodeid;
        min = 0;
        max = n - 1; 
        if(key <= kvs[max].key) ret = max;
        else
        {
            while(max > min)
            {
                x = (min + max) / 2;
                if(x == min)
                {
                    ret = x;
                    break;
                }
                if(key ==  kvs[x].key)
                {
                    ret = x;
                    break;
                }
                else if(key > kvs[x].key) min = x;
                else max = x;
            }
            if((x = ret) >= 0 && x < n)
            {
                while(x < n && key == kvs[x].key)
                {
                    ret = x++;
                }
            }
        }
    }
    return ret;
}

int lmap_in(LMAP *lmap, int64_t key, u32_t *list)
{
    int i = 0, k = 0, z = 0, ret = 0, n = 0;
    LMMKV *kvs = NULL;

    if(lmap && lmap->state && (n = lmap->state->count) > 0)
    {
        RWLOCK_RDLOCK(lmap->rwlock);
        k = lmap_find_slot(lmap, key);
        i = lmap_find_kv(lmap, k, key);
        do
        {
            kvs = lmap->map + lmap->slots[k].nodeid;
            if(key == kvs[i].key && i < lmap->slots[k].count)
            {
                if(key == lmap->slots[k].max)
                {
                    ret += lmap->slots[k].count - i;
                    if(list)
                    {
                        while(i < lmap->slots[k].count) list[z++] = kvs[i++].val;
                    }
                }
                else
                {
                    while(i < lmap->slots[k].count && key == kvs[i].key)
                    {
                        if(list)list[z++] = kvs[i].val;
                        ++ret;
                        ++i;
                    }
                }
            }
            i=0;
        }while(++k < n && lmap->slots[k].min == key);
        RWLOCK_UNLOCK(lmap->rwlock);
    }
    return ret;
}

int lmap_range(LMAP *lmap, int64_t from, int64_t to, u32_t *list)
{
    int i = 0, ii = 0, k = 0, kk = 0, j = 0, x = 0, z = 0, ret = 0, n = 0;
    LMMKV *kvs = NULL;

    if(lmap && lmap->state)
    {
        RWLOCK_RDLOCK(lmap->rwlock);
        k = lmap_find_slot(lmap, from);
        kk = lmap_find_slot2(lmap, to);
        i = lmap_find_kv(lmap, k, from);
        ii = lmap_find_kv2(lmap, kk, to);
        if(k == kk)
        {
            ret = ii + 1 - i;
            if(list)
            {
                kvs = lmap->map + lmap->slots[k].nodeid;
                for(x = i; x <= ii; x++) list[z++] = kvs[x].val;
            }
        }
        else
        {
            n =  lmap->slots[k].count;
            ret = n - i;
            if(list)
            {
                kvs = lmap->map + lmap->slots[k].nodeid;
                for(x = i; x < n; x++) list[z++] = kvs[x].val;
            }
            for(j = i+1; j < kk; j++)
            {
                ret += lmap->slots[j].count;
                if(list)
                {
                    kvs = lmap->map + lmap->slots[j].nodeid;
                    for(x = 0; x < lmap->slots[j].count; x++) list[z++] = kvs[x].val;
                }
            }
            ret += ii;
            if(list)
            {
                kvs = lmap->map + lmap->slots[kk].nodeid;
                for(x = 0; x < ii; x++) list[z++] = kvs[x].val;
            }
        }
        RWLOCK_UNLOCK(lmap->rwlock);
    }
    return ret;
}

int lmap_rangefrom(LMAP *lmap, int64_t key, u32_t *list) /* key = from */
{
    int i = 0, k = 0, x = 0, z = 0, ret = 0, n = 0;
    LMMKV *kvs = NULL;

    if(lmap && lmap->state)
    {
        RWLOCK_RDLOCK(lmap->rwlock);
        if((k = lmap_find_slot(lmap, key)) >= 0 && (i = lmap_find_kv(lmap, k, key)) >= 0)
        {
            kvs = lmap->map + lmap->slots[k].nodeid;
            n =  lmap->slots[k].count;
            if(list)
            {
                for(x = i; x < n; x++) list[z++] = kvs[x].val;
            }
            ret = n - i;
            for(i = k + 1; i <  lmap->state->count; i++)
            {
                ret += lmap->slots[i].count;
                if(list)
                {
                    kvs = lmap->map + lmap->slots[i].nodeid;
                    n = lmap->slots[i].count;
                    for(x = 0; x < n; x++) list[z++] = kvs[x].val;
                }
            }
        }
        RWLOCK_UNLOCK(lmap->rwlock);
    }
    //fprintf(stdout, "%s::%d k:%d ret:%d/%d\n", __FILE__, __LINE__, k, ret, z);
    return ret;
}

int lmap_rangeto(LMAP *lmap, int64_t key, u32_t *list) /* key = to */
{
    int i = 0, k = 0, x = 0, j = 0, z = 0, ret = 0, n = 0;
    LMMKV *kvs = NULL;

    if(lmap && lmap->state && (n = (lmap->state->count)) > 0)
    {
        RWLOCK_RDLOCK(lmap->rwlock);
        if((k = lmap_find_slot2(lmap, key)) >= 0 && k < n 
                && (i = lmap_find_kv2(lmap, k, key)) >= 0)
        {
            for(j = 0; j < k; j++)
            {
                ret += lmap->slots[j].count;
                if(list)
                {
                    kvs = lmap->map + lmap->slots[j].nodeid;
                    for(x = 0; x < lmap->slots[j].count; x++)
                    {
                        list[z++] = kvs[x].val;
                    }
                }
            }
            ret += i + 1;
            if(list)
            {
                kvs = lmap->map + lmap->slots[k].nodeid;
                for(x = 0; x <= i; x++)
                {
                    list[z++] = kvs[x].val;
                }
            }
        }
        RWLOCK_UNLOCK(lmap->rwlock);
    }
    //fprintf(stdout, "%s::%d k:%d ret:%d/%d\n", __FILE__, __LINE__, k, ret, z);
    return ret;
}

int lmap_ins(LMAP *lmap, int64_t *keys, int nkeys, u32_t *list)
{
    int ret = 0, i = 0, n = 0;
    u32_t *plist = list;

    if(lmap && lmap->state && keys && nkeys > 0)
    {
       for(i = 0; i < nkeys; i++) 
       {
            n = lmap_in(lmap, keys[i], plist);
            if(list) plist += n;
            ret += n;
       }
    }
    return ret;
}

int lmap_get(LMAP *lmap, u32_t no, u32_t *val)
{
    int ret = -1, n = 0;

    if(lmap)
    {
        RWLOCK_RDLOCK(lmap->rwlock);
        if((n = (lmap->vsize/sizeof(LMMV))) > 0 && no < n)
        {
            if(val) *val = lmap->vmap[no].val;
        }
        RWLOCK_UNLOCK(lmap->rwlock);
    }
    return ret;
}

int lmap_set(LMAP *lmap, u32_t no, int64_t key)
{
    int ret = -1;

    if(lmap)
    {
       RWLOCK_WRLOCK(lmap->rwlock);
       lmap_vset(lmap, no, key);
       if(lmap->vmap[no].off  < 0)
       {
           lmap_insert(lmap, no, key);
       }
       else
       {
           if(key != lmap->vmap[no].val)
           {
                lmap_remove(lmap, no);
                lmap_insert(lmap, no, key);
           }
       }
       ret = 0;
       lmap->vmap[no].val = key;
       RWLOCK_UNLOCK(lmap->rwlock);
    }
    return ret;
}

int lmap_del(LMAP *lmap, u32_t no)
{
    int ret = -1, n = 0;

    if(lmap)
    {
        RWLOCK_WRLOCK(lmap->rwlock);
        if((n = (lmap->vsize/sizeof(LMMV))) > 0 && no < n)
        {
            lmap_remove(lmap, no);
            lmap->vmap[no].off = -1;
            ret = 0;
        }
        RWLOCK_UNLOCK(lmap->rwlock);
    }
    return ret;
}

void lmap_close(LMAP *lmap)
{
    if(lmap)
    {
        if(lmap->state) munmap(lmap->state, lmap->msize);
        if(lmap->fd) close(lmap->fd);
        if(lmap->vmap) munmap(lmap->vmap, lmap->vmsize);
        if(lmap->vfd) close(lmap->vfd);
        RWLOCK_DESTROY(lmap->rwlock);
        free(lmap);
    }
    return ;
}

#ifdef LMAP_TEST
#include "timer.h"
#define MASK  120000
//gcc -o lmap lmap.c -DLMAP_TEST -DTEST_INS -DHAVE_PTHREAD -lpthread && ./lmap
int main()
{
    LMAP *lmap = NULL;
    int i = 0, j = 0, n = 0, total = 0, stat[MASK], stat2[MASK];
    int64_t val = 0, from = 0, to = 0, *res = NULL;
    int64_t inputs[256], last[256];
    int64_t all = 0;
    time_t stime = 0, etime = 0;
    void *timer = NULL;

    if((lmap = lmap_init("/tmp/1.idx")))
    {
        res = (int64_t *)calloc(60000000, sizeof(int64_t));
        TIMER_INIT(timer);
#ifdef TEST_INS
        //fprintf(stdout, "sizeof(stat):%d\n", sizeof(stat));
        memset(stat, 0, sizeof(stat));
        memset(stat2, 0, sizeof(stat2));
        srand(time(NULL));
        n = 256;
        for(i = 0; i < n; i++)
        {
            inputs[i] = rand()%MASK;
        }
        TIMER_RESET(timer);
        for(i = 1; i < 40000000; i++)
        {
           j = (rand()%n);
           val = inputs[j];
           stat[val]++;
           lmap_set(lmap, i, val);
           last[j] = i;
        }
        TIMER_SAMPLE(timer);
        fprintf(stdout, "set() 40000000 data, time used:%lld\n", PT_LU_USEC(timer));
        TIMER_RESET(timer);
        for(i = 0; i < n; i++)
        {
            lmap_del(lmap, last[i]);
        }
        TIMER_SAMPLE(timer);
        fprintf(stdout, "del() time used:%lld\n", PT_LU_USEC(timer));
        TIMER_RESET(timer);
        for(i = 0; i < n; i++)
        {
            val = inputs[i];
            stat2[val] = lmap_in(lmap, val, res);
        }
        TIMER_SAMPLE(timer);
        fprintf(stdout, "in() time used:%lld\n", PT_LU_USEC(timer));
        TIMER_RESET(timer);
        total = lmap_ins(lmap, inputs, n, NULL);
        TIMER_SAMPLE(timer);
        fprintf(stdout, "ins(keys, NULL) total:%d time used:%lld\n", total, PT_LU_USEC(timer));
        TIMER_RESET(timer);
        total = lmap_ins(lmap, inputs, n, res);
        TIMER_SAMPLE(timer);
        fprintf(stdout, "ins(keys, res:%p) total:%d time used:%lld\n", res, total, PT_LU_USEC(timer));
        for(i = 0; i < n; i++)
        {
            j = inputs[i];
            if(stat[j] != stat2[j])
                fprintf(stdout, "%d:%d/%d\n", j, stat[j], stat2[j]);
        }
#ifdef OUT_ALL
        for(i = 0; i < total; i++)
        {
            fprintf(stdout, "%d:%d\n", i, res[i]);
        }
#endif
#endif
        /*
        for(i = 0; i < lmap->state->count; i++)
        {
            fprintf(stdout, "%d:{min:%d max:%d}(%d)\n", i, lmap->slots[i].min, lmap->slots[i].max, lmap->slots[i].count);
        }
        */
#ifdef TEST_RANGE
        srand(time(NULL));
        TIMER_RESET(timer);
        for(i = 1; i < 40000000; i++)
        {
            val = 1356969600 + (rand()%31536000);
            lmap_set(lmap, i, val);
        }
        TIMER_SAMPLE(timer);
        fprintf(stdout, "set() 40000000 timestamps,  time used:%lld\n", PT_LU_USEC(timer));
        srand(time(NULL));
        TIMER_RESET(timer);
        all = 0;
        for(i = 0; i < 1000; i++)
        {
            val = 1356969600 + (rand()%31536000);
            all += lmap_rangefrom(lmap, val, res);
        }
        TIMER_SAMPLE(timer);
        fprintf(stdout, "rangefrom() 1000 times total:%lld, time used:%lld\n", (long long int)all, PT_LU_USEC(timer));
        srand(time(NULL));
        TIMER_RESET(timer);
        all = 0;
        for(i = 0; i < 1000; i++)
        {
            val = 1356969600 + (rand()%31536000);
            all += lmap_rangeto(lmap, val, res);
        }
        TIMER_SAMPLE(timer);
        fprintf(stdout, "rangeto() 1000 times total:%lld, time used:%lld\n", (long long int)all, PT_LU_USEC(timer));
        srand(time(NULL));
        TIMER_RESET(timer);
        all = 0;
        for(i = 0; i < 1000; i++)
        {
            from = 1356969600 + (rand()%31536000);
            to = from + rand()%31536000;
            all += lmap_range(lmap, from, to, res);
        }
        TIMER_SAMPLE(timer);
        fprintf(stdout, "range(%p) 1000 times total:%lld, time used:%lld\n", res, (long long int)all, PT_LU_USEC(timer));
        srand(time(NULL));
        TIMER_RESET(timer);
        all = 0;
        for(i = 0; i < 1000; i++)
        {
            from = 1356969600 + (rand()%31536000);
            to = from + rand()%31536000;
            all += lmap_range(lmap, from, to, NULL);
        }
        TIMER_SAMPLE(timer);
        fprintf(stdout, "range(null) 1000 times total:%lld, time used:%lld\n", (long long int)all, PT_LU_USEC(timer));

#endif
        lmap_close(lmap);
        TIMER_CLEAN(timer);
        free(res);
    }
}
#endif