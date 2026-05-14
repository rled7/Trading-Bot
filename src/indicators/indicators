/**
 * AlgoForge — src/indicators/indicators.c
 * Full implementation of all indicator math. Pure C17, zero allocation preferred.
 * Uses stack buffers via VLAs and malloc only for large temporaries.
 */

#include "indicators/indicators.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ── Private macros ── */
#define AF_ABS(x)      ((x) < 0.0 ? -(x) : (x))
#define AF_MAX(a,b)    ((a) > (b) ? (a) : (b))
#define AF_MIN(a,b)    ((a) < (b) ? (a) : (b))
#define GUARD(cond)    if (!(cond)) return AF_ERR_INVALID_PARAM

/* ── Helpers ── */
void af_fill_nan(double *a, size_t n) {
    static const double NV = 0.0/0.0;
    for (size_t i = 0; i < n; i++) a[i] = NV;
}

double af_linreg_slope(const double *src, size_t n, int period) {
    if (!src || (int)n < period || period < 2) return 0.0;
    int s = (int)n - period;
    double sx=0,sy=0,sxx=0,sxy=0;
    for (int i=0;i<period;i++) {
        double x=(double)i, y=src[s+i];
        sx+=x; sy+=y; sxx+=x*x; sxy+=x*y;
    }
    double d = period*sxx - sx*sx;
    return fabs(d)<AF_EPSILON ? 0.0 : (period*sxy - sx*sy)/d;
}

int af_crossover(const double *s1, const double *s2, size_t i) {
    if (i<1) return 0;
    int now  = s1[i]   > s2[i];
    int prev = s1[i-1] > s2[i-1];
    if (now&&!prev) return  1;
    if (!now&&prev) return -1;
    return 0;
}

AF_Error af_wilder_ema(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    af_fill_nan(out, n);
    int seed_start=-1, run=0;
    for (int i=0;i<(int)n;i++){
        if (!AF_IS_NAN(src[i])){run++;if(run==period){seed_start=i-period+1;break;}}
        else run=0;
    }
    if (seed_start<0) return AF_ERR_INSUFFICIENT_DATA;
    double a=1.0/(double)period, sum=0.0;
    for (int i=seed_start;i<seed_start+period;i++) sum+=src[i];
    int seed_end=seed_start+period-1;
    out[seed_end]=sum/(double)period;
    for (size_t i=(size_t)seed_end+1;i<n;i++)
        out[i]=src[i]*a+out[i-1]*(1.0-a);
    return AF_OK;
}

/* ══ TREND ══════════════════════════════════════════════════════════════════ */

AF_Error af_sma(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    af_fill_nan(out,n);
    /* Find first valid window of `period` non-NaN values */
    int seed=-1, run=0;
    for (int i=0;i<(int)n;i++){
        if (!AF_IS_NAN(src[i])){run++;if(run==period){seed=i-period+1;break;}}
        else run=0;
    }
    if (seed<0) return AF_ERR_INSUFFICIENT_DATA;
    double s=0;
    for (int i=seed;i<seed+period;i++) s+=src[i];
    int se=seed+period-1;
    out[se]=s/(double)period;
    for (size_t i=(size_t)se+1;i<n;i++){
        s+=src[i]-src[i-(size_t)period];
        out[i]=s/(double)period;
    }
    return AF_OK;
}

AF_Error af_ema(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    af_fill_nan(out,n);

    /* Find the first index where we have `period` consecutive valid (non-NaN) values */
    int seed_start = -1;
    int run = 0;
    for (int i=0;i<(int)n;i++){
        if (!AF_IS_NAN(src[i])) { run++; if (run==period){seed_start=i-period+1;break;} }
        else run=0;
    }
    if (seed_start < 0) return AF_ERR_INSUFFICIENT_DATA;

    double a = 2.0/(double)(period+1), s=0;
    for (int i=seed_start;i<seed_start+period;i++) s+=src[i];
    int seed_end = seed_start+period-1;
    out[seed_end]=s/(double)period;
    for (size_t i=(size_t)seed_end+1;i<n;i++)
        out[i]=src[i]*a+out[i-1]*(1.0-a);
    return AF_OK;
}

AF_Error af_wma(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<period) return AF_ERR_INSUFFICIENT_DATA;
    double wsum=(double)period*((double)period+1.0)/2.0;
    for (size_t i=(size_t)period-1;i<n;i++) {
        double s=0;
        for (int j=0;j<period;j++)
            s+=src[i-(size_t)(period-1-j)]*(double)(j+1);
        out[i]=s/wsum;
    }
    return AF_OK;
}

AF_Error af_hma(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=2);
    int half=(int)sqrt((double)period/2.0); /* WMA(n/2) */
    if (half<1) half=1;
    int sq=(int)sqrt((double)period);
    if (sq<2) sq=2;
    double *wh=(double*)malloc(n*sizeof(double));
    double *wf=(double*)malloc(n*sizeof(double));
    double *raw=(double*)malloc(n*sizeof(double));
    if (!wh||!wf||!raw){free(wh);free(wf);free(raw);return AF_ERR_OUT_OF_MEMORY;}
    af_wma(src,n,half,wh);
    af_wma(src,n,period,wf);
    for (size_t i=0;i<n;i++)
        raw[i]=(AF_IS_NAN(wh[i])||AF_IS_NAN(wf[i]))?AF_NaN:2.0*wh[i]-wf[i];
    af_wma(raw,n,sq,out);
    free(wh);free(wf);free(raw);
    return AF_OK;
}

AF_Error af_vwma(const double *src, const double *vol, size_t n, int period, double *out) {
    GUARD(src&&vol&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period-1;i<n;i++) {
        double pv=0,v=0;
        for (int j=0;j<period;j++) {
            double vv=vol[i-(size_t)j]; if (vv<=0)vv=1;
            pv+=src[i-(size_t)j]*vv; v+=vv;
        }
        out[i]=(v>AF_EPSILON)?pv/v:src[i];
    }
    return AF_OK;
}

AF_Error af_dema(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    double *e1=(double*)malloc(n*sizeof(double));
    double *e2=(double*)malloc(n*sizeof(double));
    if (!e1||!e2){free(e1);free(e2);return AF_ERR_OUT_OF_MEMORY;}
    af_ema(src,n,period,e1); af_ema(e1,n,period,e2);
    for (size_t i=0;i<n;i++)
        out[i]=(AF_IS_NAN(e1[i])||AF_IS_NAN(e2[i]))?AF_NaN:2.0*e1[i]-e2[i];
    free(e1);free(e2); return AF_OK;
}

AF_Error af_tema(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    double *e1=(double*)malloc(n*sizeof(double));
    double *e2=(double*)malloc(n*sizeof(double));
    double *e3=(double*)malloc(n*sizeof(double));
    if (!e1||!e2||!e3){free(e1);free(e2);free(e3);return AF_ERR_OUT_OF_MEMORY;}
    af_ema(src,n,period,e1); af_ema(e1,n,period,e2); af_ema(e2,n,period,e3);
    for (size_t i=0;i<n;i++)
        out[i]=(AF_IS_NAN(e1[i])||AF_IS_NAN(e2[i])||AF_IS_NAN(e3[i]))?AF_NaN
               :3.0*e1[i]-3.0*e2[i]+e3[i];
    free(e1);free(e2);free(e3); return AF_OK;
}

AF_Error af_macd(const double *src, size_t n, int fast, int slow, int sig,
                  double *om, double *os, double *oh) {
    GUARD(src&&om&&os&&oh);
    double *ef=(double*)malloc(n*sizeof(double));
    double *es=(double*)malloc(n*sizeof(double));
    if (!ef||!es){free(ef);free(es);return AF_ERR_OUT_OF_MEMORY;}
    af_ema(src,n,fast,ef); af_ema(src,n,slow,es);
    for (size_t i=0;i<n;i++)
        om[i]=(AF_IS_NAN(ef[i])||AF_IS_NAN(es[i]))?AF_NaN:ef[i]-es[i];
    af_ema(om,n,sig,os);
    for (size_t i=0;i<n;i++)
        oh[i]=(AF_IS_NAN(om[i])||AF_IS_NAN(os[i]))?AF_NaN:om[i]-os[i];
    free(ef);free(es); return AF_OK;
}

AF_Error af_adx(const double *h, const double *l, const double *c, size_t n, int period,
                 double *oa, double *op, double *om) {
    GUARD(h&&l&&c&&oa&&op&&om&&period>=1);
    if ((int)n<period*2+1){af_fill_nan(oa,n);af_fill_nan(op,n);af_fill_nan(om,n);return AF_ERR_INSUFFICIENT_DATA;}
    double a=1.0/(double)period;
    double *tr=(double*)malloc(n*sizeof(double));
    double *pdm=(double*)malloc(n*sizeof(double));
    double *mdm=(double*)malloc(n*sizeof(double));
    if (!tr||!pdm||!mdm){free(tr);free(pdm);free(mdm);return AF_ERR_OUT_OF_MEMORY;}
    tr[0]=pdm[0]=mdm[0]=0;
    for (size_t i=1;i<n;i++) {
        double hl=h[i]-l[i], hpc=AF_ABS(h[i]-c[i-1]), lpc=AF_ABS(l[i]-c[i-1]);
        tr[i]=AF_MAX(hl,AF_MAX(hpc,lpc));
        double up=h[i]-h[i-1], dn=l[i-1]-l[i];
        pdm[i]=(up>dn&&up>0)?up:0;
        mdm[i]=(dn>up&&dn>0)?dn:0;
    }
    af_fill_nan(oa,n);af_fill_nan(op,n);af_fill_nan(om,n);
    double atr_v=0,pdm_s=0,mdm_s=0;
    for (int i=1;i<=period;i++){atr_v+=tr[i];pdm_s+=pdm[i];mdm_s+=mdm[i];}
    for (size_t i=(size_t)period;i<n;i++) {
        if (i>(size_t)period){
            atr_v=atr_v*(1-a)+tr[i];
            pdm_s=pdm_s*(1-a)+pdm[i];
            mdm_s=mdm_s*(1-a)+mdm[i];
        }
        double pdi=(atr_v>AF_EPSILON)?100.0*pdm_s/atr_v:0;
        double mdi=(atr_v>AF_EPSILON)?100.0*mdm_s/atr_v:0;
        op[i]=pdi; om[i]=mdi;
        double denom=pdi+mdi;
        double dx=(denom>AF_EPSILON)?100.0*AF_ABS(pdi-mdi)/denom:0;
        oa[i]=(i==(size_t)period)?dx:oa[i-1]*(1-a)+dx*a;
    }
    free(tr);free(pdm);free(mdm); return AF_OK;
}

AF_Error af_sar(const double *h, const double *l, size_t n,
                 double start, double step, double max,
                 double *os, double *ob) {
    GUARD(h&&l&&os&&ob&&n>=2);
    os[0]=l[0]; ob[0]=1.0;
    double ep=h[0], accel=start;
    for (size_t i=1;i<n;i++) {
        int bull=(int)(ob[i-1]>0.5);
        double sar,nep=ep;
        if (bull) {
            sar=os[i-1]+accel*(ep-os[i-1]);
            sar=AF_MIN(sar,AF_MIN(l[i-1],(i>=2?l[i-2]:l[i-1])));
            if (l[i]<sar){sar=ep;nep=l[i];accel=start;bull=0;}
            else if (h[i]>ep){nep=h[i];accel=AF_MIN(accel+step,max);}
        } else {
            sar=os[i-1]-accel*(os[i-1]-ep);
            sar=AF_MAX(sar,AF_MAX(h[i-1],(i>=2?h[i-2]:h[i-1])));
            if (h[i]>sar){sar=ep;nep=h[i];accel=start;bull=1;}
            else if (l[i]<ep){nep=l[i];accel=AF_MIN(accel+step,max);}
        }
        ep=nep; os[i]=sar; ob[i]=bull?1.0:0.0;
    }
    return AF_OK;
}

AF_Error af_ichimoku(const double *h, const double *l, const double *c, size_t n,
                      int tp, int kp, int sbp, int shift,
                      double *ot, double *ok, double *osa, double *osb, double *och) {
    GUARD(h&&l&&c&&ot&&ok&&n>0);
    af_fill_nan(ot,n); af_fill_nan(ok,n);
    if (osa) af_fill_nan(osa,n);
    if (osb) af_fill_nan(osb,n);
    if (och) af_fill_nan(och,n);
    for (size_t i=0;i<n;i++) {
        /* Tenkan */
        if ((int)i>=tp-1){
            double hi=h[i],lo=l[i];
            for (int j=1;j<tp;j++){if(h[i-j]>hi)hi=h[i-j];if(l[i-j]<lo)lo=l[i-j];}
            ot[i]=(hi+lo)/2.0;
        }
        /* Kijun */
        if ((int)i>=kp-1){
            double hi=h[i],lo=l[i];
            for (int j=1;j<kp;j++){if(h[i-j]>hi)hi=h[i-j];if(l[i-j]<lo)lo=l[i-j];}
            ok[i]=(hi+lo)/2.0;
        }
    }
    if (osa) {
        for (size_t i=0;i+(size_t)shift<n;i++)
            if (!AF_IS_NAN(ot[i])&&!AF_IS_NAN(ok[i]))
                osa[i+(size_t)shift]=(ot[i]+ok[i])/2.0;
    }
    if (osb) {
        for (size_t i=0;i<n;i++) {
            if ((int)i<sbp-1) continue;
            double hi=h[i],lo=l[i];
            for (int j=1;j<sbp;j++){if(h[i-j]>hi)hi=h[i-j];if(l[i-j]<lo)lo=l[i-j];}
            size_t dst=i+(size_t)shift;
            if (dst<n) osb[dst]=(hi+lo)/2.0;
        }
    }
    if (och)
        for (size_t i=(size_t)shift;i<n;i++) och[i-(size_t)shift]=c[i];
    return AF_OK;
}

/* ══ MOMENTUM ════════════════════════════════════════════════════════════════ */

AF_Error af_rsi(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<=period) return AF_ERR_INSUFFICIENT_DATA;
    double a=1.0/(double)period, ag=0,al=0;
    for (int i=1;i<=period;i++) {
        double d=src[i]-src[i-1];
        if (d>0)ag+=d; else al+=-d;
    }
    ag/=(double)period; al/=(double)period;
    for (size_t i=(size_t)period;i<n;i++) {
        if (i>(size_t)period){
            double d=src[i]-src[i-1];
            ag=ag*(1-a)+(d>0?d:0)*a;
            al=al*(1-a)+(d<0?-d:0)*a;
        }
        double rs=(al>AF_EPSILON)?ag/al:100.0;
        out[i]=100.0-100.0/(1.0+rs);
    }
    return AF_OK;
}

AF_Error af_stochastic(const double *h, const double *l, const double *c, size_t n,
                        int kp, int dp, int smooth, double *ok, double *od) {
    GUARD(h&&l&&c&&ok&&od&&kp>=1);
    af_fill_nan(ok,n); af_fill_nan(od,n);
    if ((int)n<kp) return AF_ERR_INSUFFICIENT_DATA;
    double *rk=(double*)malloc(n*sizeof(double));
    if (!rk) return AF_ERR_OUT_OF_MEMORY;
    af_fill_nan(rk,n);
    for (size_t i=(size_t)kp-1;i<n;i++){
        double lo=l[i],hi=h[i];
        for (int j=1;j<kp;j++){if(l[i-j]<lo)lo=l[i-j];if(h[i-j]>hi)hi=h[i-j];}
        double r=hi-lo;
        rk[i]=(r>AF_EPSILON)?100.0*(c[i]-lo)/r:50.0;
    }
    af_sma(rk,n,smooth,ok);
    af_sma(ok,n,dp,od);
    free(rk); return AF_OK;
}

AF_Error af_cci(const double *h, const double *l, const double *c, size_t n,
                 int period, double constant, double *out) {
    GUARD(h&&l&&c&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period-1;i<n;i++) {
        double sum=0;
        for (int j=0;j<period;j++) sum+=(h[i-j]+l[i-j]+c[i-j])/3.0;
        double mean=sum/(double)period,mad=0;
        for (int j=0;j<period;j++) mad+=AF_ABS((h[i-j]+l[i-j]+c[i-j])/3.0-mean);
        mad/=(double)period;
        double tp=(h[i]+l[i]+c[i])/3.0;
        out[i]=(mad>AF_EPSILON)?(tp-mean)/(constant*mad):0.0;
    }
    return AF_OK;
}

AF_Error af_williams_r(const double *h, const double *l, const double *c, size_t n,
                        int period, double *out) {
    GUARD(h&&l&&c&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period-1;i<n;i++){
        double hi=h[i],lo=l[i];
        for (int j=1;j<period;j++){if(h[i-j]>hi)hi=h[i-j];if(l[i-j]<lo)lo=l[i-j];}
        double r=hi-lo;
        out[i]=(r>AF_EPSILON)?-100.0*(hi-c[i])/r:-50.0;
    }
    return AF_OK;
}

AF_Error af_roc(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<=period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period;i<n;i++){
        double p=src[i-(size_t)period];
        out[i]=(AF_ABS(p)>AF_EPSILON)?(src[i]-p)/p*100.0:0.0;
    }
    return AF_OK;
}

AF_Error af_mfi(const double *h, const double *l, const double *c,
                 const double *v, size_t n, int period, double *out) {
    GUARD(h&&l&&c&&v&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<=period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period;i<n;i++){
        double pmf=0,nmf=0;
        for (int j=0;j<period;j++){
            size_t k=i-(size_t)j;
            double tp=(h[k]+l[k]+c[k])/3.0;
            double mf=tp*v[k];
            double pp=(k>0)?(h[k-1]+l[k-1]+c[k-1])/3.0:tp;
            if (tp>=pp) pmf+=mf; else nmf+=mf;
        }
        double r=(nmf>AF_EPSILON)?pmf/nmf:100.0;
        out[i]=100.0-100.0/(1.0+r);
    }
    return AF_OK;
}

AF_Error af_trix(const double *src, size_t n, int period, int sig,
                  double *ot, double *os) {
    GUARD(src&&ot&&period>=1);
    double *e1=(double*)malloc(n*sizeof(double));
    double *e2=(double*)malloc(n*sizeof(double));
    double *e3=(double*)malloc(n*sizeof(double));
    if (!e1||!e2||!e3){free(e1);free(e2);free(e3);return AF_ERR_OUT_OF_MEMORY;}
    af_ema(src,n,period,e1);af_ema(e1,n,period,e2);af_ema(e2,n,period,e3);
    af_fill_nan(ot,n);
    for (size_t i=1;i<n;i++)
        if (!AF_IS_NAN(e3[i])&&!AF_IS_NAN(e3[i-1])&&AF_ABS(e3[i-1])>AF_EPSILON)
            ot[i]=(e3[i]-e3[i-1])/e3[i-1]*100.0;
    if (os) af_ema(ot,n,sig,os);
    free(e1);free(e2);free(e3); return AF_OK;
}

AF_Error af_momentum(const double *src, size_t n, int period, double *out) {
    GUARD(src&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<=period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period;i<n;i++) out[i]=src[i]-src[i-(size_t)period];
    return AF_OK;
}

/* ══ VOLATILITY ══════════════════════════════════════════════════════════════ */

AF_Error af_true_range(const double *h, const double *l, const double *c, size_t n, double *out) {
    GUARD(h&&l&&c&&out&&n>0);
    out[0]=h[0]-l[0];
    for (size_t i=1;i<n;i++){
        double hl=h[i]-l[i], hpc=AF_ABS(h[i]-c[i-1]), lpc=AF_ABS(l[i]-c[i-1]);
        out[i]=AF_MAX(hl,AF_MAX(hpc,lpc));
    }
    return AF_OK;
}

AF_Error af_atr(const double *h, const double *l, const double *c, size_t n, int period, double *out) {
    GUARD(h&&l&&c&&out&&period>=1);
    double *tr=(double*)malloc(n*sizeof(double));
    if (!tr) return AF_ERR_OUT_OF_MEMORY;
    af_true_range(h,l,c,n,tr);
    AF_Error e=af_wilder_ema(tr,n,period,out);
    free(tr); return e;
}

AF_Error af_bollinger(const double *src, size_t n, int period, double mult,
                       double *ou, double *om, double *ol, double *obw, double *opb) {
    GUARD(src&&ou&&om&&ol&&period>=1);
    af_sma(src,n,period,om);
    af_fill_nan(ou,n); af_fill_nan(ol,n);
    if (obw) af_fill_nan(obw,n);
    if (opb) af_fill_nan(opb,n);
    for (size_t i=(size_t)period-1;i<n;i++){
        if (AF_IS_NAN(om[i])) continue;
        double var=0;
        for (int j=0;j<period;j++){double d=src[i-(size_t)j]-om[i];var+=d*d;}
        double sd=sqrt(var/(double)period);
        ou[i]=om[i]+mult*sd; ol[i]=om[i]-mult*sd;
        if (obw&&om[i]>AF_EPSILON) obw[i]=(ou[i]-ol[i])/om[i]*100.0;
        double r=ou[i]-ol[i];
        if (opb&&r>AF_EPSILON) opb[i]=(src[i]-ol[i])/r;
    }
    return AF_OK;
}

AF_Error af_keltner(const double *h, const double *l, const double *c, size_t n,
                     int ep, int ap, double mult, double *ou, double *om, double *ol) {
    GUARD(h&&l&&c&&ou&&om&&ol);
    double *atr=(double*)malloc(n*sizeof(double));
    if (!atr) return AF_ERR_OUT_OF_MEMORY;
    af_ema(c,n,ep,om); af_atr(h,l,c,n,ap,atr);
    for (size_t i=0;i<n;i++){
        if (AF_IS_NAN(om[i])||AF_IS_NAN(atr[i])){ou[i]=ol[i]=AF_NaN;continue;}
        ou[i]=om[i]+mult*atr[i]; ol[i]=om[i]-mult*atr[i];
    }
    free(atr); return AF_OK;
}

AF_Error af_donchian(const double *h, const double *l, size_t n, int period,
                      double *ou, double *om, double *ol) {
    GUARD(h&&l&&ou&&ol&&period>=1);
    af_fill_nan(ou,n); af_fill_nan(ol,n);
    if (om) af_fill_nan(om,n);
    if ((int)n<period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period-1;i<n;i++){
        double hi=h[i],lo=l[i];
        for (int j=1;j<period;j++){if(h[i-j]>hi)hi=h[i-j];if(l[i-j]<lo)lo=l[i-j];}
        ou[i]=hi; ol[i]=lo;
        if (om) om[i]=(hi+lo)*0.5;
    }
    return AF_OK;
}

AF_Error af_hist_vol(const double *src, size_t n, int period, int tdays, double *out) {
    GUARD(src&&out&&period>=2);
    af_fill_nan(out,n);
    if ((int)n<=period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period;i<n;i++){
        double mean=0;
        for (int j=0;j<period;j++){
            double p=src[i-1-j];
            if (p>AF_EPSILON) mean+=log(src[i-j]/p);
        }
        mean/=(double)period;
        double var=0;
        for (int j=0;j<period;j++){
            double p=src[i-1-j];
            if (p>AF_EPSILON){double r=log(src[i-j]/p)-mean;var+=r*r;}
        }
        double sd=sqrt(var/(double)(period-1));
        out[i]=sd*sqrt((double)tdays)*100.0;
    }
    return AF_OK;
}

/* ══ VOLUME ══════════════════════════════════════════════════════════════════ */

AF_Error af_obv(const double *c, const double *v, size_t n, double *out) {
    GUARD(c&&v&&out&&n>0);
    out[0]=0;
    for (size_t i=1;i<n;i++){
        double d=c[i]-c[i-1];
        out[i]=out[i-1]+((d>0)?v[i]:(d<0)?-v[i]:0.0);
    }
    return AF_OK;
}

AF_Error af_vwap(const double *h, const double *l, const double *c, const double *v, size_t n,
                  double *ov, double *ou1, double *ou2, double *ol1, double *ol2) {
    GUARD(h&&l&&c&&v&&ov&&n>0);
    double cpv=0,cv=0,ctp=0,ctp2=0; size_t cnt=0;
    for (size_t i=0;i<n;i++){
        double vv=v[i]>0?v[i]:1;
        double tp=(h[i]+l[i]+c[i])/3.0;
        cpv+=tp*vv; cv+=vv; ctp+=tp; ctp2+=tp*tp; cnt++;
        ov[i]=cpv/cv;
        double mean=ctp/(double)cnt;
        double var=ctp2/(double)cnt-mean*mean;
        double sd=(var>0)?sqrt(var):0;
        if (ou1) ou1[i]=ov[i]+sd;
        if (ou2) ou2[i]=ov[i]+2*sd;
        if (ol1) ol1[i]=ov[i]-sd;
        if (ol2) ol2[i]=ov[i]-2*sd;
    }
    return AF_OK;
}

AF_Error af_cmf(const double *h, const double *l, const double *c, const double *v,
                 size_t n, int period, double *out) {
    GUARD(h&&l&&c&&v&&out&&period>=1);
    af_fill_nan(out,n);
    if ((int)n<period) return AF_ERR_INSUFFICIENT_DATA;
    for (size_t i=(size_t)period-1;i<n;i++){
        double mfvs=0,vs=0;
        for (int j=0;j<period;j++){
            double r=h[i-j]-l[i-j];
            double clv=(r>AF_EPSILON)?((c[i-j]-l[i-j])-(h[i-j]-c[i-j]))/r:0;
            mfvs+=clv*v[i-j]; vs+=v[i-j];
        }
        out[i]=(vs>AF_EPSILON)?mfvs/vs:0;
    }
    return AF_OK;
}

AF_Error af_acc_dist(const double *h, const double *l, const double *c,
                      const double *v, size_t n, double *out) {
    GUARD(h&&l&&c&&v&&out&&n>0);
    out[0]=0;
    for (size_t i=0;i<n;i++){
        double r=h[i]-l[i];
        double clv=(r>AF_EPSILON)?((c[i]-l[i])-(h[i]-c[i]))/r:0;
        out[i]=(i==0?0:out[i-1])+clv*v[i];
    }
    return AF_OK;
}

AF_Error af_force_index(const double *c, const double *v, size_t n, int period, double *out) {
    GUARD(c&&v&&out&&n>=2&&period>=1);
    double *raw=(double*)malloc(n*sizeof(double));
    if (!raw) return AF_ERR_OUT_OF_MEMORY;
    raw[0]=0;
    for (size_t i=1;i<n;i++) raw[i]=(c[i]-c[i-1])*v[i];
    AF_Error e=af_ema(raw,n,period,out);
    free(raw); return e;
}

AF_Error af_vol_osc(const double *v, size_t n, int fast, int slow, double *out) {
    GUARD(v&&out);
    double *ef=(double*)malloc(n*sizeof(double));
    double *es=(double*)malloc(n*sizeof(double));
    if (!ef||!es){free(ef);free(es);return AF_ERR_OUT_OF_MEMORY;}
    af_ema(v,n,fast,ef); af_ema(v,n,slow,es);
    for (size_t i=0;i<n;i++)
        out[i]=(!AF_IS_NAN(ef[i])&&!AF_IS_NAN(es[i])&&AF_ABS(es[i])>AF_EPSILON)
               ?(ef[i]-es[i])/es[i]*100.0:AF_NaN;
    free(ef);free(es); return AF_OK;
}

/* ══ S/R ═════════════════════════════════════════════════════════════════════ */

AF_Error af_pivot_classic(double h, double l, double c,
                            double *r3,double *r2,double *r1,double *p,
                            double *s1,double *s2,double *s3) {
    GUARD(r3&&r2&&r1&&p&&s1&&s2&&s3);
    *p=(h+l+c)/3.0;
    *r1=2.0**p-l; *r2=*p+(h-l); *r3=*p+2.0*(h-l);
    *s1=2.0**p-h; *s2=*p-(h-l); *s3=*p-2.0*(h-l);
    return AF_OK;
}
AF_Error af_pivot_fibonacci(double h, double l, double c,
                              double *r3,double *r2,double *r1,double *p,
                              double *s1,double *s2,double *s3) {
    GUARD(r3&&r2&&r1&&p&&s1&&s2&&s3);
    double r=h-l;
    *p=(h+l+c)/3.0;
    *r1=*p+r*0.382; *r2=*p+r*0.618; *r3=*p+r*1.000;
    *s1=*p-r*0.382; *s2=*p-r*0.618; *s3=*p-r*1.000;
    return AF_OK;
}
AF_Error af_pivot_camarilla(double h, double l, double c,
                              double *r4,double *r3,double *r2,double *r1,
                              double *s1,double *s2,double *s3,double *s4) {
    GUARD(r4&&r3&&r2&&r1&&s1&&s2&&s3&&s4);
    double r=h-l;
    *r4=c+r*1.1/2.0; *r3=c+r*1.1/4.0; *r2=c+r*1.1/6.0; *r1=c+r*1.1/12.0;
    *s1=c-r*1.1/12.0; *s2=c-r*1.1/6.0; *s3=c-r*1.1/4.0; *s4=c-r*1.1/2.0;
    return AF_OK;
}
AF_Error af_fibonacci(double sh, double sl, bool up, double *fl, size_t count) {
    GUARD(fl&&count>=7);
    static const double F[]={0.0,0.236,0.382,0.500,0.618,0.786,1.000};
    double r=sh-sl;
    for (size_t i=0;i<7&&i<count;i++)
        fl[i]=up?(sh-F[i]*r):(sl+F[i]*r);
    return AF_OK;
}
