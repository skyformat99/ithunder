#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ibase.h"
#include "db.h"
#include "mdb.h"
#include "timer.h"
#include "zvbcode.h"
#include "logger.h"
#include "xmm.h"
#include "mtree64.h"
#include "imap.h"
#include "lmap.h"
#include "dmap.h"
#include "immx.h"
#include "bmap.h"
/* push XMAP */
void ibase_push_xmap(IBASE *ibase, XMAP *xmap)
{
    int x = 0;

    if(ibase && xmap)
    {
        MUTEX_LOCK(ibase->mutex_xmap);
        if(ibase->nqxmaps < IB_XMAPS_MAX)
        {
            x = ibase->nqxmaps++;
            ibase->qxmaps[x] = xmap;
        }
        else
        {
            xmm_free(xmap, sizeof(XMAP));
        }
        MUTEX_UNLOCK(ibase->mutex_xmap);
    }
    return ;
}

/* ibase pop xmap */
XMAP *ibase_pop_xmap(IBASE *ibase)
{
    XMAP *xmap = NULL;
    int x = 0;

    if(ibase)
    {
        MUTEX_LOCK(ibase->mutex_xmap);
        if(ibase->nqxmaps > 0)
        {
            x = --(ibase->nqxmaps);
            xmap = ibase->qxmaps[x];
            ibase->qxmaps[x] = NULL;
        }
        else
        {
            xmap = (XMAP *)xmm_new(sizeof(XMAP));
        }
        xmap->count = 0;
        MUTEX_UNLOCK(ibase->mutex_xmap);
    }
    return xmap;
}
#define BITXHIT(ibase, xmap, old, pxnode, _m_)                                  \
do                                                                              \
{                                                                               \
    _m_ = old->nhits++;                                                         \
    old->hits[_m_] = pxnode->which;                                             \
    if(xmap->flag & IB_QUERY_IGNRANK) break;                                    \
    if(xmap->is_query_qhits)                                                    \
    {                                                                           \
        if(!(pxnode->bithits & old->bithits)) old->nvhits += pxnode->nvhits;    \
        old->bithits |= pxnode->bithits;                                        \
    }                                                                           \
    if(xmap->is_query_ffilter)    old->bitfields |= pxnode->bitfields;          \
    if(xmap->is_query_bitfhit)                                                  \
    {                                                                           \
        old->bitfhit |= pxnode->bitfhit;                                        \
        old->bitfnot |= pxnode->bitfnot;                                        \
    }                                                                           \
    if(xmap->qfhits && (pxnode->bitfields & xmap->qfhits)) old->nhitfields++;   \
    if(xmap->is_query_phrase)                                                   \
    {                                                                           \
        if(pxnode->no < old->xmax)                                              \
        {                                                                       \
            while(_m_ > 0 && pxnode->no < old->bitphrase[_m_-1])                \
            {                                                                   \
                old->bitphrase[_m_] = old->bitphrase[_m_-1];                    \
                old->bitquery[_m_] = old->bitquery[_m_-1];                      \
                --_m_;                                                          \
            }                                                                   \
        }                                                                       \
        old->bitphrase[_m_] = pxnode->no;                                       \
        old->bitquery[_m_] = pxnode->which;                                     \
        if(pxnode->no < old->xmin) old->xmin = pxnode->no;                      \
        else if(pxnode->no > old->xmax) old->xmax = pxnode->no;                 \
    }                                                                           \
}while(0)
#define XMAP_RESET(ibase, hmap, secid)
void xmap_push(IBASE *ibase, XMAP *xmap, XNODE *pxnode)
{
    int _x_ = 0, _i_ = 0, _n_ = 0, _z_ = 0, _to_ = 0;
    XNODE **xnodes = NULL, *old = NULL;

    xnodes = xmap->xnodes;
    if(xmap->count == 0)
    {
        //ACCESS_LOGGER(ibase->logger, "docid:%d count:%d", pxnode->docid, xmap->count);
        xmap->min = xmap->max = 0;
        xnodes[0] = pxnode;
        xmap->count++;
    }
    else if(xmap->min <= xmap->max)
    {
        //ACCESS_LOGGER(ibase->logger, "docid:%d min:%d[%d] max:%d[%d] count:%d", pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
        _x_ = xmap->min;
        _to_ = xmap->max;
        if(pxnode->docid == xnodes[_x_]->docid)
        {
            //ACCESS_LOGGER(ibase->logger, "docid:%d min:%d[%d] max:%d[%d] count:%d", pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
            _i_ = _x_;
            old = xnodes[_i_];BITXHIT(ibase, xmap, old, pxnode, _n_);
        }
        else if(pxnode->docid == xnodes[_to_]->docid)
        {
            //ACCESS_LOGGER(ibase->logger, "docid:%d min:%d[%d] max:%d[%d] count:%d", pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
            _i_ = _to_;
            old = xnodes[_i_];BITXHIT(ibase, xmap, old, pxnode, _n_);
        }
        else if(pxnode->docid > xnodes[_to_]->docid)
        {
            //ACCESS_LOGGER(ibase->logger, "docid:%d min:%d[%d] max:%d[%d] count:%d", pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
            if((xmap->max + 1) == IB_XNODE_MAX)
            {
                _i_ = xmap->min = 0;
                while(_x_ <= xmap->max) xnodes[_i_++] = xnodes[_x_++];
                xnodes[_i_] = pxnode;
                xmap->max = _i_;
            }
            else
            {
                _i_ = ++(xmap->max);
                xnodes[_i_] = pxnode;
            }
            xmap->count++;
        }
        else if(pxnode->docid < xnodes[_x_]->docid)
        {
            //ACCESS_LOGGER(ibase->logger, "docid:%d min:%d[%d] max:%d[%d] count:%d", pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
            if(_x_ > 0)
            {
                _i_ = --(xmap->min);
                xnodes[_i_] = pxnode;
            }
            else
            {
                _i_ = ++(xmap->max);
                while(_i_ > 0)
                {
                    xnodes[_i_] = xnodes[_i_-1];
                    --_i_;
                }
                xnodes[0] = pxnode;
            }
            xmap->count++;
        }
        else
        {
            //ACCESS_LOGGER(ibase->logger, "docid:%d min:%d[%d] max:%d[%d] count:%d", pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
            _i_ = -1;
            _x_ = xmap->min;
            _to_ = xmap->max;
            while(_to_ > _x_)
            {
                _z_ = (_x_ + _to_)/2;
                if(_z_ == _x_){_i_ = _z_; break;}
                if(pxnode->docid == xnodes[_z_]->docid){_i_ = _z_; break;}
                else if(pxnode->docid > xnodes[_z_]->docid) _x_ = _z_;
                else _to_ = _z_;
            }
            if(_i_ >= 0)
            {

                if(xnodes[_i_]->docid == pxnode->docid)
                {
                    //ACCESS_LOGGER(ibase->logger, "docid:%d xnode[%d] => [%p] min:%d[%d] max:%d[%d] count:%d", pxnode->docid, _i_, xnodes[_i_], xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
                    old = xnodes[_i_];BITXHIT(ibase, xmap, old, pxnode, _n_);
                }
                else
                {
                    if(xmap->max < (IB_XNODE_MAX-1) || xmap->min == 0)
                    {
                        //ACCESS_LOGGER(ibase->logger, "docid:%d min:%d[%d] max:%d[%d] count:%d", pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
                        _i_ = (xmap->max)++;
                        while(_i_ > 0 && pxnode->docid < xnodes[_i_]->docid)
                        {
                            xnodes[_i_+1] = xnodes[_i_];
                            --_i_;
                        }
                        xnodes[_i_+1] = pxnode;
                        //ACCESS_LOGGER(ibase->logger, "i:%d docid:%d min:%d[%d] max:%d[%d] count:%d", _i_+1, pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
                    }
                    else
                    {
                    //ACCESS_LOGGER(ibase->logger, "docid:%d min:%d[%d] max:%d[%d] count:%d", pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
                        if(xmap->max == (IB_XNODE_MAX-1))
                        {
                            _to_ = xmap->max;
                            _x_ = xmap->min;
                            _i_ = xmap->min = 0;
                            while(_x_ < _to_ && pxnode->docid > xnodes[_x_]->docid)
                            {
                                xnodes[_i_] = xnodes[_x_];
                                ++_i_;
                                ++_x_;
                            }
                            xnodes[_i_] = pxnode;
                            //ACCESS_LOGGER(ibase->logger, "i:%d docid:%d min:%d[%d] max:%d[%d] count:%d", _i_, pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
                            while(_x_ <= _to_) xnodes[++_i_] = xnodes[_x_++];
                            xmap->max = _i_;
                        }
                        else
                        {
                            _to_ = xmap->max;
                            _i_ = (xmap->min)--;
                            while(_i_ < _to_ && pxnode->docid > xnodes[_i_]->docid)
                            {
                                xnodes[_i_-1] = xnodes[_i_];
                                ++_i_;
                            }
                            xnodes[_i_-1] = pxnode;
                            //ACCESS_LOGGER(ibase->logger, "i:%d docid:%d min:%d[%d] max:%d[%d] count:%d", _i_-1, pxnode->docid, xmap->min, xnodes[xmap->min]->docid, xmap->max,  xnodes[xmap->max]->docid, xmap->count);
                        }
                    }
                    xmap->count++;
                }
            }
        }
    }
    return ;
}

XNODE *xmap_pop(IBASE *ibase, XMAP *xmap)
{
    XNODE **xnodes = NULL;
    XNODE *pxnode = NULL;

    if(xmap->count > 0 && xmap->min >= 0 && xmap->min < IB_XNODE_MAX && (xnodes = xmap->xnodes))
    {
        pxnode = xnodes[xmap->min];
        xnodes[xmap->min] = NULL;
        ++(xmap->min);
        if(--(xmap->count) == 0) xmap->max = xmap->min;
    }
    return pxnode;
}

void ibase_unindex(IBASE *ibase, ITERM *itermlist, XMAP *_xmap_, 
        int _x_, int *merge_total)
{
    int _n_ = 0 , *_np_ = NULL;

    if(itermlist[_x_].p < itermlist[_x_].end)
    {
        if(ibase->state->compression_status != IB_COMPRESSION_DISABLED)
        {
            itermlist[_x_].ndocid = 0;
            itermlist[_x_].term_count = 0;
            itermlist[_x_].no = 0;
            itermlist[_x_].fields = 0;
            itermlist[_x_].prevnext_size = 0;
            _np_ = &(itermlist[_x_].ndocid);
            UZVBCODE(itermlist[_x_].p, _n_, _np_);
            itermlist[_x_].docid +=  itermlist[_x_].ndocid;
            _np_ = &(itermlist[_x_].term_count);
            UZVBCODE(itermlist[_x_].p, _n_, _np_);
            _np_ = (int *)&(itermlist[_x_].no);
            UZVBCODE(itermlist[_x_].p, _n_, _np_);
            _np_ = &(itermlist[_x_].fields);
            UZVBCODE(itermlist[_x_].p, _n_, _np_);
            _np_ = &(itermlist[_x_].prevnext_size);
            UZVBCODE(itermlist[_x_].p, _n_, _np_);
        }
        else
        {
            itermlist[_x_].docid = *((int*)itermlist[_x_].p);
            itermlist[_x_].p += sizeof(int);
            itermlist[_x_].term_count = *((int*)itermlist[_x_].p);
            itermlist[_x_].p += sizeof(int);
            itermlist[_x_].no = *((int*)itermlist[_x_].p);
            itermlist[_x_].p += sizeof(int);
            itermlist[_x_].fields = *((int*)itermlist[_x_].p);
            itermlist[_x_].p += sizeof(int);
            itermlist[_x_].prevnext_size = *((int*)itermlist[_x_].p);
            itermlist[_x_].p += sizeof(int);
        }
        itermlist[_x_].xnode.which = _x_;
        itermlist[_x_].xnode.docid = itermlist[_x_].docid;
        itermlist[_x_].xnode.hits[0] = _x_;
        itermlist[_x_].xnode.nhits = 1;
        if(_xmap_->flag & IB_QUERY_IGNRANK) goto end;                                    
        if(_xmap_->is_query_ffilter)
            itermlist[_x_].xnode.bitfields = itermlist[_x_].fields;
        if(_xmap_->qfhits)
        {
            if(itermlist[_x_].fields & _xmap_->qfhits)
                itermlist[_x_].xnode.nhitfields = 1;
            else
                itermlist[_x_].xnode.nhitfields = 0;
        }
        if(_xmap_->is_query_bitfhit)
        {
            itermlist[_x_].xnode.bitfhit = 0;
            itermlist[_x_].xnode.bitfnot = 0;
            if((itermlist[_x_].fields & itermlist[_x_].bitfhit) == itermlist[_x_].bitfhit) 
                itermlist[_x_].xnode.bitfhit |= 1 << itermlist[_x_].synno;
            if(itermlist[_x_].fields & itermlist[_x_].bitfnot) 
                itermlist[_x_].xnode.bitfnot |= 1 << itermlist[_x_].synno;
        }
        if(_xmap_->is_query_qhits)
        {
            itermlist[_x_].xnode.bithits = 1 << itermlist[_x_].synno;
            if(itermlist[_x_].weight)
            {
                itermlist[_x_].xnode.nvhits = 1;
            }
            else
            {
                itermlist[_x_].xnode.nvhits = 0;
            }
        }
        if(_xmap_->is_query_phrase)
        {
            if(itermlist[_x_].prevnext_size > 0
                    && (itermlist[_x_].eposting - itermlist[_x_].sposting) 
                    >= itermlist[_x_].prevnext_size)
            {
                itermlist[_x_].sprevnext = itermlist[_x_].sposting;
                itermlist[_x_].eprevnext = itermlist[_x_].sposting 
                    + itermlist[_x_].prevnext_size;
                itermlist[_x_].sposting += itermlist[_x_].prevnext_size;
            }
            else
            {
                itermlist[_x_].sprevnext = NULL;
                itermlist[_x_].eprevnext = NULL;
            }
            _n_ = itermlist[_x_].no;
            itermlist[_x_].xnode.bitphrase[0] = _n_;
            itermlist[_x_].xnode.bitquery[0] = _x_;
            itermlist[_x_].xnode.no = _n_;
            itermlist[_x_].xnode.xmin = _n_;
            itermlist[_x_].xnode.xmax = _n_;
        }
end:
        if(merge_total) (*merge_total)++;
        return xmap_push(ibase, _xmap_, &(itermlist[_x_].xnode));
    }
    return ;
}
 
/* binary list merging */
ICHUNK *ibase_bquery(IBASE *ibase, IQUERY *query, int secid)
{
    int i = 0, x = 0, n = 0, mm = 0, nn = 0, k = 0, z = 0, *np = NULL, nqterms = 0, nquerys = 0, 
        is_query_phrase =  0, docid = 0, ifrom = -1, is_sort_reverse = 0, gid = 0, max = 0, 
        int_index_from = 0, int_index_to = 0, ito = -1, double_index_from = 0, xno = 0, min = 0,
        double_index_to = 0, range_flag = 0, prev = 0, last = -1, no = 0, next = 0, fid = 0, 
        nxrecords = 0, is_field_sort = 0, scale = 0, is_groupby = 0, total = 0, ignore_rank = 0, 
        long_index_from = 0, long_index_to = 0, kk = 0, nx = 0, prevnext = 0, ii = 0, jj = 0, 
        imax = 0, imin = 0,xint = 0,bitfhit = 0,booland=0,j = 0, *vint = NULL, merge_total = 0;
        //syns[IB_SYNTERM_MAX], 
    double score = 0.0, p1 = 0.0, p2 = 0.0, dfrom = 0.0, *vdouble = NULL,
           tf = 1.0, Py = 0.0, Px = 0.0, dto = 0.0, xdouble = 0.0;
    int64_t bits = 0, lfrom = 0, lto = 0, base_score = 0, *vlong = NULL,
            doc_score = 0, old_score = 0, xdata = 0, xlong = 0;
    void *timer = NULL, *topmap = NULL, *fmap = NULL, *groupby = NULL, 
         *index = NULL, *posting = NULL;
    IRECORD *record = NULL, *records = NULL, xrecords[IB_NTOP_MAX];
    IHEADER *headers = NULL; ICHUNK *chunk = NULL;
    XMAP *xmap = NULL; XNODE *xnode = NULL;
    TERMSTATE *termstates = NULL;
    ITERM *itermlist = NULL;
    IRES *res = NULL;

    if(ibase && query && secid >= 0 && secid < IB_SEC_MAX 
            && (index = ibase->mindex[secid]) 
            && (posting = ibase->mposting[secid]))
    {
        if((chunk = ibase_pop_chunk(ibase)))
        {
            res = &(chunk->res);
            memset(res, 0, sizeof(IRES));
            res->qid = query->qid;
            records = chunk->records;
        }
        else
        {
            goto end;
        }
        xmap = ibase_pop_xmap(ibase);
        itermlist = ibase_pop_itermlist(ibase);
        topmap = ibase_pop_stree(ibase);
        fmap = ibase_pop_stree(ibase);
        headers = (IHEADER *)(ibase->state->headers[secid].map);
        if(topmap == NULL || fmap == NULL || xmap == NULL 
                || itermlist == NULL || headers == NULL) goto end;
        if((p1 = query->ravgdl) <= 0.0) p1 = 1.0;
        if((query->flag & IB_QUERY_RSORT)) is_sort_reverse = 1;        
        else if((query->flag & IB_QUERY_SORT)) is_sort_reverse = 0;
        else is_sort_reverse = 1;
        int_index_from = ibase->state->int_index_from;
        int_index_to = int_index_from + ibase->state->int_index_fields_num;
        long_index_from = ibase->state->long_index_from;
        long_index_to = long_index_from + ibase->state->long_index_fields_num;
        double_index_from = ibase->state->double_index_from;
        double_index_to = double_index_from + ibase->state->double_index_fields_num;
        nqterms = query->nqterms;
        if(query->nqterms > IB_QUERY2_MAX) nqterms = IB_QUERY2_MAX;
        nquerys = query->nvqterms;
        if(query->nvqterms > IB_QUERY2_MAX) nquerys = IB_QUERY2_MAX;
        if(nquerys > 0) scale = query->hitscale[nquerys-1];
        XMAP_RESET(ibase, xmap, secid);
        if(ibase->state->phrase_status != IB_PHRASE_DISABLED && (query->flag & IB_QUERY_PHRASE)) 
            xmap->is_query_phrase = 1;
        else xmap->is_query_phrase = 0;
        if(query->flag & IB_QUERY_FIELDS)  xmap->is_query_bitfhit = 1;
        else xmap->is_query_bitfhit = 0;
        if(query->fields_filter != 0 && query->fields_filter != -1) xmap->is_query_ffilter = 1;
        else xmap->is_query_ffilter = 0;
        if((query->flag & IB_QUERY_BOOLAND)) booland = 1;
        if(scale>0||booland||query->qweight)xmap->is_query_qhits = 1;
        else xmap->is_query_qhits = 0;
        xmap->flag = query->flag;
        xmap->qfhits = query->qfhits;
        xmap->itermlist = itermlist;
        fid = query->orderby;
        if(fid >= int_index_from && fid < int_index_to)
        {
            fid -= int_index_from;
            fid += IB_INT_OFF;
            if(ibase->state->mfields[secid][fid]) is_field_sort = IB_SORT_BY_INT;
        }
        else if(fid >= long_index_from && fid < long_index_to)
        {
            fid -= long_index_from;
            fid += IB_LONG_OFF;
            if(ibase->state->mfields[secid][fid]) is_field_sort = IB_SORT_BY_LONG;
        }
        else if(fid >= double_index_from && fid < double_index_to)
        {
            fid -= double_index_from;
            fid += IB_DOUBLE_OFF;
            if(ibase->state->mfields[secid][fid]) is_field_sort = IB_SORT_BY_DOUBLE;
        }
        gid = query->groupby;
        if(gid >= int_index_from && gid < int_index_to)
        {
            gid -= int_index_from;
            gid += IB_INT_OFF;
            if(ibase->state->mfields[secid][gid]) is_groupby  = IB_GROUPBY_INT;
        }
        else if(gid >= long_index_from && gid < long_index_to)
        {
            gid -= long_index_from;
            gid += IB_LONG_OFF;
            if(ibase->state->mfields[secid][gid]) is_groupby  = IB_GROUPBY_LONG;
        }
        else if(gid >= double_index_from && gid < double_index_to)
        {
            gid -= double_index_from;
            gid += IB_DOUBLE_OFF;
            if(ibase->state->mfields[secid][gid]) is_groupby  = IB_GROUPBY_DOUBLE;
        }
        if(gid > 0){groupby = ibase_pop_mmx(ibase);};
        TIMER_INIT(timer);
        //read index 
        nn = nqterms;
        for(i = 0; i < nqterms; i++)
        {
            itermlist[i].idf = query->qterms[i].idf;
            itermlist[i].termid = query->qterms[i].id;
            itermlist[i].synno = query->qterms[i].synno;
            itermlist[i].bitfhit = query->qterms[i].bitfhit;
            itermlist[i].bitfnot = query->qterms[i].bitfnot;
            bitfhit |= 1 << query->qterms[i].synno;
            if((query->qterms[i].flag & QTERM_BIT_DOWN) && query->qweight) 
            {
                itermlist[i].weight = 0;
            }
            else 
            {
                itermlist[i].weight = 1;
            }
            if((n = itermlist[i].mm.ndata = mdb_get_data(PMDB(index), 
                            itermlist[i].termid, &(itermlist[i].mm.data))) > 0)
            {
                /* reading index */
                total += n;
                itermlist[i].p = itermlist[i].mm.data;
                itermlist[i].end = itermlist[i].mm.data + itermlist[i].mm.ndata;
                itermlist[i].docid = 0;
                itermlist[i].last = -1;
                MUTEX_LOCK(ibase->mutex_termstate);
                termstates = (TERMSTATE *)(ibase->termstateio.map);
                itermlist[i].term_len = termstates[itermlist[i].termid].len;
                MUTEX_UNLOCK(ibase->mutex_termstate);
                x = i;
                /* reading posting */
                if(is_query_phrase && (itermlist[i].posting.ndata = 
                        mdb_get_data(PMDB(posting), itermlist[i].termid, 
                                &(itermlist[i].posting.data))) > 0)
                {
                    itermlist[i].sposting = itermlist[i].posting.data;
                    itermlist[i].eposting = itermlist[i].posting.data 
                        + itermlist[i].posting.ndata;
                }
                else
                {
                   // WARN_LOGGER(ibase->logger, "is_query_phrase:%d posting:%p termid:%d sposting:%s eposting:%p", is_query_phrase, posting, itermlist[i].termid, itermlist[i].sposting, itermlist[i].eposting);
                    itermlist[i].sposting = NULL;
                    itermlist[i].eposting = NULL;
                }
                ibase_unindex(ibase, itermlist, xmap, x, &merge_total);
            }
        }
        TIMER_SAMPLE(timer);
        res->io_time = (int)PT_LU_USEC(timer);
        DEBUG_LOGGER(ibase->logger, "reading index data qid:%d terms:%d vqterms:%d querys:%d bytes:%d time used :%lld", query->qid, query->nqterms, query->nvqterms, query->nquerys, total, PT_LU_USEC(timer));
        res->total = 0;
        while((xnode = xmap_pop(ibase, xmap)))
        {
            docid = xnode->docid;
            if(last != -1 && docid < last)
            {
                FATAL_LOGGER(ibase->logger, "qid:%d docid:%d last:%d min:%d max:%d count:%d", query->qid, docid, last, xmap->min, xmap->max, xmap->count);
                _exit(-1);
            }
            last = docid;
            ignore_rank = 0;
            doc_score = 0.0;
            base_score = 0.0;
            score = 0.0;
            if(xmap->flag & IB_QUERY_IGNRANK) goto next;
#ifdef IB_USE_BMAP
            if(query->flag & IB_QUERY_BMAP)
            {
                if(bmap_check(ibase->bmaps[secid], docid) == 0) goto next;
            }
            else
            {
                if(headers[docid].status < 0 || headers[docid].globalid == 0) goto next;
            }
#else
            if(!(query->flag & IB_QUERY_IGNSTATUS))
            {
                if(headers[docid].status < 0 || headers[docid].globalid == 0) goto next;
            }
#endif
            /* check fobidden terms in query string */
            if((query->flag & IB_QUERY_FORBIDDEN) && headers[docid].status<IB_SECURITY_OK)goto next;
            /* secure level */
            if((k = headers[docid].slevel) < 0 || headers[docid].slevel > IB_SLEVEL_MAX || query->slevel_filter[k]  == 1) goto next;
            /* boolen check */
            if(query->operators.bitsnot && (query->operators.bitsnot & xnode->bithits))
                    goto next;
            if((query->flag & IB_QUERY_BOOLAND) && query->operators.bitsand 
                && (query->nvqterms != query->nquerys 
                    || (query->operators.bitsand & xnode->bithits) != query->operators.bitsand))
                    goto next;
            /* catetory block filter */
            if(query->catblock_filter && (query->catblock_filter & headers[docid].category)) 
            {
                goto next;
            }
            /* multicat filter */
            if(query->multicat_filter != 0 && (query->multicat_filter & headers[docid].category) != query->multicat_filter)
                goto next;
            /* fields filter */
            if(xmap->is_query_ffilter && !(query->fields_filter & xnode->bitfields))
                goto next;
            if(xmap->is_query_bitfhit)
            {
                //WARN_LOGGER(ibase->logger, "docid:%d bitnot:%d", docid, xnode->bitnot);
                if(xnode->bitfnot) goto next;
                if(xnode->bitfhit != bitfhit) goto next;
            }
            if(booland && query->operators.bitsand && (query->nvqterms != query->nquerys 
                    || (query->operators.bitsand & xnode->bithits) != query->operators.bitsand))
                    goto next;
            if(!booland && nquerys > 0)
            {
                if(xmap->is_query_qhits && xnode->nvhits == 0) goto next;
                if(((xnode->nvhits * 100) / nquerys) < scale) goto next;
            }
            DEBUG_LOGGER(ibase->logger, "docid:%d/%lld nvhits:%d nquerys:%d/%d scale:%d int[%d/%d] catgroup:%d", docid, LL(headers[docid].globalid), xnode->nvhits, query->nqterms, nquerys, scale, query->int_range_count, query->int_bits_count, query->catgroup_filter);
            /* in  filter */
            for(j = 0; j < query->int_in_num; j++)
            {
                if((jj = query->int_in_list[j].fieldid) > 0 
                        && (imax = (query->int_in_list[j].num - 1)) >= 0
                        && (jj += (IB_INT_OFF - int_index_from)) > 0 
                        && ibase->state->mfields[secid][jj]
                        && (vint = query->int_in_list[j].vals))
                {
                    imin = 0;
                    xint = IMAP_GET(ibase->state->mfields[secid][jj], docid);
                    if(xint < vint[imin] || xint > vint[imax]) goto next;
                    if(xint != vint[imin] && xint != vint[imax])
                    {
                        while(imax > imin)
                        {
                            ii = (imax + imin) / 2; 
                            if(ii == imin)break;
                            if(xint == vint[ii]) break;
                            else if(xint > vint[ii]) imin = ii;
                            else imax = ii;
                        }
                        if(xint != vint[ii]) goto next;
                    }
                }
            }
            for(j = 0; j < query->long_in_num; j++)
            {
                if((jj = query->long_in_list[j].fieldid) > 0 
                        && (imax = (query->long_in_list[j].num - 1)) >= 0
                        && (jj += (IB_LONG_OFF - long_index_from)) > 0 
                        && ibase->state->mfields[secid][jj]
                        && (vlong = query->long_in_list[j].vals))
                {
                    imin = 0;
                    xlong = LMAP_GET(ibase->state->mfields[secid][jj], docid);
                    if(xlong < vlong[imin] || xlong > vlong[imax]) goto next;
                    if(xlong != vlong[imin] && xlong != vlong[imax])
                    {
                        while(imax > imin)
                        {
                            ii = (imax + imin) / 2; 
                            if(ii == imin)break;
                            if(xlong == vlong[ii]) break;
                            else if(xlong > vlong[ii]) imin = ii;
                            else imax = ii;
                        }
                        if(xlong != vlong[ii]) goto next;
                    }
                }
            }
            for(j = 0; j < query->double_in_num; j++)
            {
                if((jj = query->double_in_list[j].fieldid) > 0 
                        && (imax = (query->double_in_list[j].num - 1)) >= 0
                        && (jj += (IB_DOUBLE_OFF - double_index_from)) > 0 
                        && ibase->state->mfields[secid][jj]
                        && (vdouble = query->double_in_list[j].vals))
                {
                    imin = 0;
                    xdouble = DMAP_GET(ibase->state->mfields[secid][jj], docid);
                    if(xdouble < vdouble[imin] || xdouble > vdouble[imax]) goto next;
                    if(xdouble != vdouble[imin] && xdouble != vdouble[imax])
                    {
                        while(imax > imin)
                        {
                            ii = (imax + imin) / 2; 
                            if(ii == imin)break;
                            if(xdouble == vdouble[ii]) break;
                            else if(xdouble > vdouble[ii]) imin = ii;
                            else imax = ii;
                        }
                        if(xdouble != vdouble[ii]) goto next;
                    }
                }
            }
            /* range filter */
            if((query->int_range_count > 0 || query->int_bits_count > 0))
            {
                for(i = 0; i < query->int_range_count; i++)
                {
                    k = query->int_range_list[i].field_id;
                    range_flag = query->int_range_list[i].flag;
                    ifrom = query->int_range_list[i].from;
                    ito = query->int_range_list[i].to;
                    k -= int_index_from;
                    k += IB_INT_OFF;
                    if(!ibase->state->mfields[secid][k]) goto next;
                    xint = IMAP_GET(ibase->state->mfields[secid][k], docid);
                    if((range_flag & IB_RANGE_FROM) && xint < ifrom) goto next;
                    if((range_flag & IB_RANGE_TO) && xint > ito) goto next;
                }
                for(i = 0; i < query->int_bits_count; i++)
                {
                    if(query->int_bits_list[i].bits == 0) continue;
                    k = query->int_bits_list[i].field_id;
                    k -= int_index_from;
                    k += IB_INT_OFF;
                    if(!ibase->state->mfields[secid][k]) goto next;
                    xint = IMAP_GET(ibase->state->mfields[secid][k], docid);
                    if((query->int_bits_list[i].flag & IB_BITFIELDS_FILTER))
                    {
                        if((query->int_bits_list[i].bits & xint) == 0) goto next;
                    }
                    else
                    {
                        if((query->int_bits_list[i].bits & xint)) goto next;
                    }
                }
            }
            if((query->long_range_count > 0 || query->long_bits_count > 0))
            {
                for(i = 0; i < query->long_range_count; i++)
                {
                    k = query->long_range_list[i].field_id;
                    range_flag = query->long_range_list[i].flag;
                    lfrom = query->long_range_list[i].from;
                    lto = query->long_range_list[i].to;
                    k -= long_index_from;
                    k += IB_LONG_OFF;
                    if(!ibase->state->mfields[secid][k]) goto next;
                    xlong = LMAP_GET(ibase->state->mfields[secid][k], docid);
                    if((range_flag & IB_RANGE_FROM) && xlong < lfrom) goto next;
                    if((range_flag & IB_RANGE_TO) && xlong > lto) goto next;
                }
                for(i = 0; i < query->long_bits_count; i++)
                {
                    if(query->int_bits_list[i].bits == 0) continue;
                    k = query->long_bits_list[i].field_id;
                    k -= long_index_from;
                    k += IB_LONG_OFF;
                    if(!ibase->state->mfields[secid][k]) goto next;
                    xlong = LMAP_GET(ibase->state->mfields[secid][k], docid);
                    if((query->long_bits_list[i].flag & IB_BITFIELDS_FILTER))
                    {
                        if((query->long_bits_list[i].bits & xlong) == 0) goto next;
                    }
                    else
                    {
                        if((query->long_bits_list[i].bits & xlong)) goto next;
                    }
                }
            }
            if(query->double_range_count > 0)
            {
                for(i = 0; i < query->double_range_count; i++)
                {
                    k = query->double_range_list[i].field_id;
                    range_flag = query->double_range_list[i].flag;
                    dfrom = query->double_range_list[i].from;
                    dto = query->double_range_list[i].to;
                    k -= double_index_from;
                    k += IB_DOUBLE_OFF;
                    if(!ibase->state->mfields[secid][k]) goto next;
                    xdouble = DMAP_GET(ibase->state->mfields[secid][k], docid);
                    if((range_flag & IB_RANGE_FROM) && xdouble < dfrom) goto next;
                    if((range_flag & IB_RANGE_TO) && xdouble > dto) goto next;
                }
            }
            /* category grouping  */
            if((bits = (query->catgroup_filter & headers[docid].category)))
            {
                k = 0;
                do
                {
                    if((bits & (int64_t)0x01))
                    {
                        if(res->catgroups[k] == 0) res->ncatgroups++;
                        res->catgroups[k]++;
                    }
                    bits >>= 1;
                    ++k;
                }while(bits);
            }
            /* cat filter */
            if(query->category_filter != 0 && (query->category_filter & headers[docid].category) == 0)
                goto next;

            DEBUG_LOGGER(ibase->logger, "catgroup docid:%d/%lld category:%lld nbits:%d nhitsfields:%d", docid, IBLL(headers[docid].globalid), IBLL(headers[docid].category), xnode->nhits, xnode->nhitfields);
            /* hits/fields hits */
            if(xmap->is_query_qhits) 
                base_score += IBLONG((xnode->nhits + xnode->nvhits) * query->base_hits);
            if(xmap->qfhits) base_score += IBLONG(xnode->nhitfields * query->base_fhits);
            mm = xnode->nhits - 1;
            nn = 0;
            DEBUG_LOGGER(ibase->logger, "docid:%d/%lld base_score:%lld score:%f doc_score:%lld min:%d max:%d", docid, IBLL(headers[docid].globalid), IBLL(base_score), score, IBLL(doc_score), nn, mm);
            /* scoring */
            i = --(xnode->nhits);
            do
            {
                x = xnode->hits[i];
                /* phrase */
                if(xmap->is_query_phrase && itermlist[x].sprevnext)
                {
                    no = 0;
                    nx = 0;
                    xno = -1;
                    do
                    {
                        next = -1;
                        prev = -1;
                        z = 0;
                        np = &z;
                        UZVBCODE(itermlist[x].sprevnext, n, np);
                        nx += z;
                        no = nx >> 1;
                        if(nx & 0x01) next = no;
                        else prev = no;
                        if(no < xnode->xmin)continue;
                        if(no > xnode->xmax)break;
                        if(no != xno)
                        {
                            min = nn;max = mm;
                            k = -1;
                            do
                            {
                                if(no == xnode->bitphrase[min]){k = min;break;}
                                if(no == xnode->bitphrase[max]){k = max;break;}
                                z = (max + min)/2;
                                if(z == min) break;
                                if(no == xnode->bitphrase[z]){k = z;break;}
                                else if(no > xnode->bitphrase[z])min = z;
                                else max = z;
                            }while(max > min);
                        }
                        if(k >= 0 && no == xnode->bitphrase[k] 
                                && !(query->qterms[x].flag & QTERM_BIT_DOWN)) 
                        {
                            kk = xnode->bitquery[k];
                            prevnext = 0;
                            if(prev >= 0 && (query->qterms[x].prev & (1 << kk)))
                            {
                                //DEBUG_LOGGER(ibase->logger, "docid:%d/%lld phrase_next:%d x:%d--kk:%d", docid, IBLL(headers[docid].globalid), query->qterms[x].next, x, kk);
                                prevnext += 2;
                            }
                            else if(next >= 0 && (query->qterms[x].next & (1 << kk)))
                            {
                                //WARN_LOGGER(ibase->logger, "docid:%d/%lld phrase_next:%d x:%d kk:%d", docid, IBLL(headers[docid].globalid), next, x, kk);
                                prevnext += 2;
                            }
                            if(prevnext)
                            {
                                if((itermlist[x].fields & query->qfhits) 
                                        && (itermlist[kk].fields & query->qfhits))
                                {
                                    prevnext += 4;
                                }
                                base_score += IBLONG(query->base_phrase*prevnext);
                            }
                        }
                        xno = no;
                    }while(itermlist[x].sprevnext < itermlist[x].eprevnext);
                }
                if(query->flag & IB_QUERY_RELEVANCE)
                {
                    tf = (double)(itermlist[x].term_count)
                        /(double)(headers[docid].terms_total);
                    p2 = (double)(headers[docid].terms_total) / p1;
                    Py = itermlist[x].idf * tf * IB_BM25_P1;
                    Px = tf + IB_BM25_K1 - IB_BM25_P2 + IB_BM25_P2 * p2;
                    score += (Py/Px);
                    DEBUG_LOGGER(ibase->logger, "i:%d term:%d docid:%d/%lld p1:%f p2:%f ttotal:%d tf:%f idf:%f score:%f", i, itermlist[x].termid, docid, IBLL(headers[docid].globalid), p1, p2, headers[docid].terms_total, tf, itermlist[x].idf, score);
                }
                base_score += IBLONG(query->qterms[x].size * query->base_nterm);
                if(itermlist[x].weight)
                {
                    base_score += IBLONG(query->qterms[x].size * query->base_nterm);
                }
                //xnode->bitphrase[i] = 0;
                //xnode->bitquery[i] = 0;
                ibase_unindex(ibase, itermlist, xmap, x, &merge_total);
            }while(--i >= 0);
            DEBUG_LOGGER(ibase->logger, "docid:%d/%lld base_score:%lld score:%f doc_score:%lld", docid, IBLL(headers[docid].globalid), IBLL(base_score), score, IBLL(doc_score));
            /* bitxcat */
            if(headers[docid].category != 0)
            {
                if(query->bitxcat_up != 0 && (headers[docid].category & query->bitxcat_up))
                    base_score += IBLONG(query->base_xcatup);
                else if(query->bitxcat_down != 0 && (headers[docid].category & query->bitxcat_down))
                {
                    ignore_rank = 1;
                    if(base_score > IBLONG(query->base_xcatdown))
                        base_score -= IBLONG(query->base_xcatdown);
                    else
                        base_score = 0ll;
                }
            }
            doc_score = IB_LONG_SCORE(((double)base_score+(double)score));
            DEBUG_LOGGER(ibase->logger, "docid:%d/%lld base_score:%lld score:%f doc_score:%lld", docid, IBLL(headers[docid].globalid), IBLL(base_score), score, IBLL(doc_score));
            /* rank */
            if(ignore_rank == 0 && (query->flag & IB_QUERY_RANK)) 
                doc_score += IBLONG((headers[docid].rank*(double)(query->base_rank)));
            DEBUG_LOGGER(ibase->logger, "docid:%d/%lld base_score:%lld rank:%f base_rank:%lld doc_score:%lld", docid, IBLL(headers[docid].globalid), IBLL(base_score), headers[docid].rank, IBLL(query->base_rank), IBLL(doc_score));
            /* group by */
            if(groupby && gid > 0)
            {
                if(is_groupby == IB_GROUPBY_INT)
                {
                    xlong = (int64_t)IMAP_GET(ibase->state->mfields[secid][gid], docid); 
                }
                else if(is_groupby == IB_GROUPBY_LONG)
                {
                    xlong = LMAP_GET(ibase->state->mfields[secid][gid], docid);
                }
                else if(is_groupby == IB_GROUPBY_DOUBLE)
                {
                    xlong = IB_LONG_SCORE(DMAP_GET(ibase->state->mfields[secid][gid], docid));
                }
                IMMX_ADD(groupby, xlong);
                DEBUG_LOGGER(ibase->logger, "docid:%d/%lld gid:%d key:%lld count:%d", docid, IBLL(headers[docid].globalid), gid, IBLL(xlong), PIMX(groupby)->count);
            }
            if(is_field_sort)
            {
                if(is_field_sort == IB_SORT_BY_INT)
                {
                    //i = fid + ibase->state->int_index_fields_num * docid;
                    doc_score = IB_INT2LONG_SCORE(IMAP_GET(ibase->state->mfields[secid][fid], docid)) + (int64_t)(doc_score >> 16);
                }
                else if(is_field_sort == IB_SORT_BY_LONG)
                {
                    //i = fid + ibase->state->long_index_fields_num * docid;
                    doc_score = LMAP_GET(ibase->state->mfields[secid][fid], docid);
                }
                else if(is_field_sort == IB_SORT_BY_DOUBLE)
                {
                    //i = fid + ibase->state->double_index_fields_num * docid;
                    doc_score = IB_LONG_SCORE(DMAP_GET(ibase->state->mfields[secid][fid], docid));
                }
            }
            /* sorting */
            if(MTREE64_TOTAL(topmap) >= query->ntop)
            {
                xdata = 0ll;
                if(is_sort_reverse)
                {
                    if(doc_score >= MTREE64_MINK(topmap))
                    {
                        mtree64_pop_min(MTR64(topmap), &old_score, &xdata);
                        if((record = (IRECORD *)((long )xdata)))
                        {
                            record->globalid    = (int64_t)docid;
                            mtree64_push(MTR64(topmap), doc_score, xdata);
                        }
                    }
                }
                else
                {
                    if(doc_score <= MTREE64_MAXK(topmap))
                    {
                        mtree64_pop_max(MTR64(topmap), &old_score, &xdata);
                        if((record = (IRECORD *)((long)xdata)))
                        {
                            record->globalid    = (int64_t)docid;
                            mtree64_push(MTR64(topmap), doc_score, xdata);
                        }
                    }
                }
            }
            else
            {
                if(nxrecords < IB_NTOP_MAX && (record = &(xrecords[nxrecords++])))
                {
                    record->globalid    = (int64_t)docid;
                    mtree64_push(MTR64(topmap), doc_score, (int64_t)record);
                }
            }
            res->total++;
            continue;

next:
            i = --(xnode->nhits);
            do
            {
                x = xnode->hits[i];
                xnode->bitphrase[i] = 0;
                xnode->bitquery[i] = 0;
                ibase_unindex(ibase, itermlist, xmap, x, &merge_total);
            }while(--i >= 0);
        }
        TIMER_SAMPLE(timer);
        res->sort_time = (int)PT_LU_USEC(timer);
        if((res->count = MTREE64_TOTAL(topmap)) > 0)
        {
            i = 0;
            do
            {
                xdata = 0;
                doc_score = 0;
                if(is_sort_reverse){mtree64_pop_max(MTR64(topmap), &doc_score, &xdata);}
                else{mtree64_pop_min(MTR64(topmap), &doc_score, &xdata);}
                if((record = (IRECORD *)((long)xdata)))
                {
                    docid = (int)record->globalid;
                    records[i].score = doc_score;
                    records[i].globalid = (int64_t)headers[docid].globalid;
                    DEBUG_LOGGER(ibase->logger, "top[%d/%d] docid:%d/%lld score:%lld", i, MTREE64_TOTAL(topmap), docid, IBLL(headers[docid].globalid), IBLL(doc_score));
                }
                ++i;
            }while(MTREE64_TOTAL(topmap) > 0);
            res->flag |= is_field_sort;
        }
        if(groupby && (res->ngroups = (PIMX(groupby)->count)) > 0)
        {
            i = 0;
            do
            {
                IMMX_POP_MIN(groupby, xlong, xdata);
                if(i < IB_GROUP_MAX)
                {
                    res->groups[i].key = xlong;
                    res->groups[i].val = xdata;
                }
                ++i;
            }while(PIMX(groupby)->count > 0);
            res->flag |= is_groupby;
            if(res->ngroups > IB_GROUP_MAX) 
            {
                WARN_LOGGER(ibase->logger, "invalid number %d/%d for groups[%d] qid:%d", res->ngroups, IB_GROUP_MAX, res->groups, query->qid);
                res->ngroups = IB_GROUP_MAX;
            }
        }
        REALLOG(ibase->logger, "bquery(%d) merge(%d) res(%d) documents topK(%d) time used:%lld ioTime:%lld sortTime:%lld ncatgroups:%d ngroups:%d", query->qid, merge_total, res->total, res->count, PT_USEC_U(timer), IBLL(res->io_time), IBLL(res->sort_time),res->ncatgroups, res->ngroups);
end:
        //free db blocks
        if(itermlist)
        {
            for(i = 0; i < nqterms; i++)
            {
                mdb_free_data(PMDB(index), itermlist[i].mm.data, itermlist[i].mm.ndata);
                itermlist[i].mm.data = NULL;
                itermlist[i].mm.ndata = 0;
                if(itermlist[i].posting.data)
                {
                    mdb_free_data(PMDB(posting), itermlist[i].posting.data, 
                            itermlist[i].posting.ndata);
                    itermlist[i].posting.data = NULL;
                    itermlist[i].posting.ndata = 0;
                }
            }
            ibase_push_itermlist(ibase, itermlist);
        }
        if(res) res->doctotal = ibase->state->dtotal;
        if(xmap) ibase_push_xmap(ibase, xmap);
        if(fmap) ibase_push_stree(ibase, fmap);
        if(groupby) ibase_push_mmx(ibase, groupby);
        if(topmap) ibase_push_stree(ibase, topmap);
        TIMER_CLEAN(timer);
    }
    return chunk;
}
