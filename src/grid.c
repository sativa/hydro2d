#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "geom.h"
#include "grid.h"
#include "hydro.h"
#include "par.h"

//Local Functions
double minmod(double a, double b, double c);

int set_reconstruction(struct parList *pars)
{
    int err = 0;
    int choice = pars->recon;

    if(choice == 0)
        reconstruction = &interpolate_constant;
    else if(choice == 1)
        reconstruction = &interpolate_plm;
    else
    {
        err = 1;
        printf("ERROR - Invalid Reconstruction choice: %d\n", choice);
    }

    return err;
}

void make_grid(struct grid *g, struct parList *pars)
{
    //Set grid variables from parameter list and allocate
    //space for data arrays.
    g->nx1_int = pars->nx1;
    g->nx2_int = pars->nx2;
    g->ng11 = pars->bc1Inner == 0 ? 0 : pars->nghost;
    g->ng12 = pars->bc1Outer == 0 ? 0 : pars->nghost;
    g->ng21 = pars->bc2Inner == 0 ? 0 : pars->nghost;
    g->ng22 = pars->bc2Outer == 0 ? 0 : pars->nghost;
    g->nx1 = pars->nx1 + g->ng11 + g->ng12;
    g->nx2 = pars->nx2 + g->ng21 + g->ng22;
    g->x1min = pars->x1min;
    g->x1max = pars->x1max;
    g->x2min = pars->x2min;
    g->x2max = pars->x2max;
    g->t = pars->tmin;
    g->nc = pars->nc;
    g->nq = pars->nc + pars->np;
    g->PLM = pars->plm;

    g->d1 = g->nx2 * g->nq;
    g->d2 = g->nq;

    g->x1 = (double *) malloc((g->nx1+1) * sizeof(double));
    g->x2 = (double *) malloc((g->nx2+1) * sizeof(double));
    g->prim = (double *) malloc(g->nx1 * g->nx2 * g->nq * sizeof(double));
    g->cons = (double *) malloc(g->nx1 * g->nx2 * g->nq * sizeof(double));
    g->cons_rk = (double *) malloc(g->nx1 * g->nx2 * g->nq * sizeof(double));
    g->prim_grad =  (double *) malloc(g->nx1 * g->nx2 * g->nq * 2 * sizeof(double));
    g->prim_grad2 = (double *) malloc(g->nx1 * g->nx2 * g->nq * 2 * sizeof(double));

    int i;
    double dx1 = (g->x1max - g->x1min) / g->nx1_int;
    double dx2 = (g->x2max - g->x2min) / g->nx2_int;
    for(i=0; i<g->nx1+1; i++)
        g->x1[i] = g->x1min + (i - g->ng11) * dx1;
    for(i=0; i<g->nx2+1; i++)
        g->x2[i] = g->x2min + (i - g->ng21) * dx2;
}

void free_grid(struct grid *g)
{
    free(g->x1);
    free(g->x2);
    free(g->prim);
    free(g->cons);
    free(g->cons_rk);
    free(g->prim_grad);
    free(g->prim_grad2);
}

void calc_grad(struct grid *g, struct parList *par)
{

    int nx1 = g->nx1;
    int nx2 = g->nx2;
    int d1 = g->d1;
    int d2 = g->d2;
    int nq = g->nq;

    int i,j;

    for(i=0; i<nx1; i++)
        for(j=0; j<nx2; j++)
        {
            reconstruction(g, i, j, 0, &(g->prim_grad[2*(d1*i+d2*j)+0*nq]), 
                            &(g->prim_grad2[2*(d1*i+d2*j)+0*nq]), par);
            reconstruction(g, i, j, 1, &(g->prim_grad[2*(d1*i+d2*j)+1*nq]), 
                            &(g->prim_grad2[2*(d1*i+d2*j)+1*nq]), par);
        }
}

void interpolate_constant(struct grid *g, int i, int j, int dir,
                        double gradlim[], double gradraw[], struct parList *par)
{
    int q;
    int nq = g->nq;

    if(dir != 0 && dir != 1)
    {
        printf("ERROR - interpolate constant has bad dir=%d\n", dir);
        return;
    }

    for(q=0; q<nq; q++)
    {
        gradraw[q] = 0.0;
        gradlim[q] = 0.0;
    }
}

void interpolate_plm(struct grid *g, int i, int j, int dir,
                        double gradlim[], double gradraw[], struct parList *par)
{
    int q;
    int nq = g->nq;
    int nx1 = g->nx1;
    int nx2 = g->nx2;
    int d1 = g->d1;
    int d2 = g->d2;
    int iL, jL, iR, jR;
    double plm = par->plm;

    if(dir == 0)
    {
        iL = i-1;
        iR = i+1;
        jL = j;
        jR = j;

        iL  = iL  >= 0 ? iL  : 0;
        iR  = iR  < nx1 ? iR  : nx1-1;
    }
    else if(dir == 1)
    {
        iL = i;
        iR = i;
        jL = j-1;
        jR = j+1;

        jL  = jL  >= 0 ? jL  : 0;
        jR  = jR  < nx2 ? jR  : nx2-1;
    }
    else
    {
        printf("ERROR - interpolate plm has bad dir=%d\n", dir);
        return;
    }

    double xLm[2] = {g->x1[iL], g->x2[jL]};
    double xLp[2] = {g->x1[iL+1], g->x2[jL+1]};
    double xCm[2] = {g->x1[i], g->x2[j]};
    double xCp[2] = {g->x1[i+1], g->x2[j+1]};
    double xRm[2] = {g->x1[iR], g->x2[jR]};
    double xRp[2] = {g->x1[iR+1], g->x2[jR+1]};

    double xL[2], xC[2], xR[2];
    geom_CM(xLm, xLp, xL);
    geom_CM(xCm, xCp, xC);
    geom_CM(xRm, xRp, xR);

    //If at a domain edge, fix outer x[] to exactly mirror edge zone.
    if(dir==0 && iL==i)
        xL[dir] = 2*g->x1[0] - xC[dir];
    if(dir==1 && jL==j)
        xL[dir] = 2*g->x2[0] - xC[dir];
    if(dir==0 && iR==i)
        xR[dir] = 2*g->x1[nx1] - xC[dir];
    if(dir==1 && jR==j)
        xR[dir] = 2*g->x2[nx2] - xC[dir];

    double idxCL = 1.0/(xC[dir]-xL[dir]);
    double idxRL = 1.0/(xR[dir]-xL[dir]);
    double idxRC = 1.0/(xR[dir]-xC[dir]);

    double sL, sC, sR;
    int refl[nq];
    reflectInds(dir, refl, nq);

    if((dir==0 && iL==i) || (dir==1 && jL==j))
        for(q=0; q<nq; q++)
        {
            if(refl[q])
            {
                sL = (g->prim[d1*i +d2*j +q] + g->prim[d1*i +d2*j +q])*idxCL;
                sR = (g->prim[d1*iR+d2*jR+q] - g->prim[d1*i +d2*j +q])*idxRC;
                sC = (g->prim[d1*iR+d2*jR+q] + g->prim[d1*i +d2*j +q])*idxRL;
            }
            else
            {
                sL = 0.0;
                sR = (g->prim[d1*iR+d2*jR+q] - g->prim[d1*i +d2*j +q])*idxRC;
                sC = (g->prim[d1*iR+d2*jR+q] - g->prim[d1*i +d2*j +q])*idxRL;
            }
            gradraw[q] = sC;
            gradlim[q] = minmod(plm*sL, sC, plm*sR);
        }
    else if((dir==0 && i==iR) || (dir==1 && j==jR))
        for(q=0; q<nq; q++)
        {
            if(refl[q])
            {
                sL = ( g->prim[d1*i +d2*j +q] - g->prim[d1*iL+d2*jL+q])*idxCL;
                sR = (-g->prim[d1*i +d2*j +q] - g->prim[d1*i +d2*j +q])*idxRC;
                sC = (-g->prim[d1*i +d2*j +q] - g->prim[d1*iL+d2*jL+q])*idxRL;
            }
            else
            {
                sL = ( g->prim[d1*i +d2*j +q] - g->prim[d1*iL+d2*jL+q])*idxCL;
                sR = 0.0;
                sC = ( g->prim[d1*i +d2*j +q] - g->prim[d1*iL+d2*jL+q])*idxRL;
            }
            gradraw[q] = sC;
            gradlim[q] = minmod(plm*sL, sC, plm*sR);
        }
    else
        for(q=0; q<nq; q++)
        {
            sL = (g->prim[d1*i +d2*j +q] - g->prim[d1*iL+d2*jL+q])*idxCL;
            sR = (g->prim[d1*iR+d2*jR+q] - g->prim[d1*i +d2*j +q])*idxRC;
            sC = (g->prim[d1*iR+d2*jR+q] - g->prim[d1*iL+d2*jL+q])*idxRL;
            gradraw[q] = sC;
            gradlim[q] = minmod(plm*sL, sC, plm*sR);
        }
}

void copy_to_rk(struct grid *g)
{
    // Copy cons into cons_rk.

    int i;
    int nx1 = g->nx1;
    int nx2 = g->nx2;
    int nq = g->nq;

    for(i=0; i<nx1*nx2*nq; i++)
        g->cons_rk[i] = g->cons[i];
}

void update_cons(struct grid *g, double fac1, double fac2)
{
    // Update cons with fac1*cons & fac2*cons_rk
    
    int i;
    int nx1 = g->nx1;
    int nx2 = g->nx2;
    int nq = g->nq;

    for(i=0; i<nx1*nx2*nq; i++)
        g->cons[i] = fac1 * g->cons[i] + fac2 * g->cons_rk[i];
}

void update_cons_rk(struct grid *g, double fac1, double fac2)
{
    // Update cons_rk with fac1*cons & fac2*cons_rk

    int i;
    int nx1 = g->nx1;
    int nx2 = g->nx2;
    int nq = g->nq;

    for(i=0; i<nx1*nx2*nq; i++)
        g->cons_rk[i] = fac1 * g->cons[i] + fac2 * g->cons_rk[i];
}

//Local definitions

double minmod(double a, double b, double c)
{
    double m;

    if(a*b < 0)
        m = 0;
    else if(fabs(a) < fabs(b))
        m = a;
    else
        m = b;

    if(m*c < 0)
        m = 0;
    else if(fabs(c) < fabs(m))
        m = c;

    return m;
}
