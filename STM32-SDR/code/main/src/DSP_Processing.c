/*
 * Core DSP processing code
 *
 * STM32-SDR: A software defined HAM radio embedded system.
 * Copyright (C) 2013, STM32-SDR Group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include	"Init_DMA.h"
#include	"arm_math.h"
#include	"FIR_Coefficients.h"
#include	"DSP_Processing.h"


float	RMS_Sig;
float	dB_Sig;

int		Sig;
int		Sig_Max;
int		Sig_Total;
int		Sig_Sum0;
int		Sig_Sum1;
int		Sig_Sum2;

int		AGC_Scale;
int 	number_bins;
float 	AGC_Multiplier;

float 	FFT_Coeff = 0.2;
extern  int NCO_Bin;
extern  int AGC_Mode;
extern  int RSL_Mag;
extern  float AGC_Mag;

float32_t FFT_Output[256];
float32_t FFT_Filter[256];

q15_t FFT_Input[1024];
q15_t FFT_Scale[1024]; //512 sampling
q15_t FFT_Magnitude[512]; //512 sampling

int FFT_Mag_10[256];

uint16_t FFT_Size = 512;  // change for 512 sampling
uint8_t FFT_status;

q15_t FIR_State_I[NUM_FIR_COEF + (BUFFERSIZE / 2) - 1];
q15_t FIR_State_Q[NUM_FIR_COEF + (BUFFERSIZE / 2) - 1];
q15_t FIR_I_In[BUFFERSIZE / 2];
q15_t FIR_Q_In[BUFFERSIZE / 2];
q15_t FIR_I_Out[BUFFERSIZE / 2];
q15_t FIR_Q_Out[BUFFERSIZE / 2];
q15_t USB_Out[BUFFERSIZE / 2];
q15_t LSB_Out[BUFFERSIZE / 2];

//chh Filter Definitions imported from NUE PSK
q15_t Filter_delay_I1[38]; //38 = 35 + 4 -1
q15_t Filter_delay_I2[38];
q15_t Filter_delay_Q1[38];
q15_t Filter_delay_Q2[38];
q15_t Filter_delay_I3[65]; //65 = 65 + 1 -1
q15_t Filter_delay_Q3[65];
q15_t Filter_delay_I4[65];
q15_t Filter_delay_Q4[65];
//chh Filter Definitions imported from NUE PSK



q15_t ADC_Buffer[BUFFERSIZE / 2];  //for 1024 sampling

arm_cfft_radix2_instance_q15 S_CFFT;

void init_DSP(void) {
	FFT_status = arm_cfft_radix2_init_q15(&S_CFFT, FFT_Size, 0, 1);
}

arm_fir_instance_q15 S_FIR_I = { NUM_FIR_COEF, &FIR_State_I[0], &coeff_fir_I[0] };
arm_fir_instance_q15 S_FIR_Q = { NUM_FIR_COEF, &FIR_State_Q[0], &coeff_fir_Q[0] };

//chh PSK Filters
arm_fir_decimate_instance_q15 Filter_I1 = { 4, 35, &Dec4LPCoef[0], &Filter_delay_I1[0] };
arm_fir_decimate_instance_q15 Filter_I2 = { 4, 35, &Dec4LPCoef[0], &Filter_delay_I2[0] };
arm_fir_decimate_instance_q15 Filter_Q1 = { 4, 35, &Dec4LPCoef[0], &Filter_delay_Q1[0] };
arm_fir_decimate_instance_q15 Filter_Q2 = { 4, 35, &Dec4LPCoef[0], &Filter_delay_Q2[0] };
arm_fir_instance_q15 Filter_I3 = { 65, &Filter_delay_I3[0], &FIRBitCoef[0] };
arm_fir_instance_q15 Filter_Q3 = { 65, &Filter_delay_Q3[0], &FIRBitCoef[0] };
arm_fir_instance_q15 Filter_I4 = { 65, &Filter_delay_I4[0], &FIRFreqCoef[0] };
arm_fir_instance_q15 Filter_Q4 = { 65, &Filter_delay_Q4[0], &FIRFreqCoef[0] };
//chh PSK Filters

void Process_FIR_I(void)
{
	arm_fir_q15(&S_FIR_I, &FIR_I_In[0], &FIR_I_Out[0], BUFFERSIZE / 2);
}

void Process_FIR_Q(void)
{
	arm_fir_q15(&S_FIR_Q, &FIR_Q_In[0], &FIR_Q_Out[0], BUFFERSIZE / 2);
}

void Sideband_Demod(void)
{
	int16_t j;
	for (j = 0; j < BUFFERSIZE / 2; j++) {
		USB_Out[j] = FIR_I_Out[j] - FIR_Q_Out[j];
		LSB_Out[j] = FIR_I_Out[j] + FIR_Q_Out[j];
		//Fill the PSK Buffer
		ADC_Buffer[j] = USB_Out[j]; //made change to get display to work for psk
	}
}


void Process_FFT(void)
{
	//Execute complex FFT
	arm_cfft_radix2_q15(&S_CFFT, &FFT_Input[0]);
	//Shift FFT data to compensate for FFT scaling
	arm_shift_q15(&FFT_Input[0], 6, &FFT_Scale[0], 512 );
	//Calculate the magnitude squared of FFT results ( i.e., power level)
	arm_cmplx_mag_squared_q15(&FFT_Scale[0], &FFT_Magnitude[0], FFT_Size);
	//********************************************************

	Sig_Max = 0;
	Sig_Sum0 = 0;
	Sig_Sum1 = 0;
	Sig_Sum2 = 0;

	number_bins =0;
	AGC_Multiplier = (float)AGC_Scale/100.0;



	for (int j = 0; j<256;j++) FFT_Mag_10[j] = (int) FFT_Magnitude[j] * 10; //add in 10 db gain
	for (int16_t j = 8; j < 252; j++)
	{  //Changed for 512 FFT
		//This detects bins which are saturated and ignores them
		if (FFT_Mag_10[j]> 0)
		{
			FFT_Output[j] =  (10.0 * log((float32_t)FFT_Mag_10[j] + 1.0));
		}
			else
		{
			FFT_Output[j] = 0;
		}
		//First Order Filter for FFT
		FFT_Filter[j] =  FFT_Coeff * FFT_Output[j] + (1.0-FFT_Coeff) * FFT_Filter[j];

		if (j == NCO_Bin)
		{
			for (int k = NCO_Bin-2; k < NCO_Bin+3; k++)
			{
				Sig = (int)FFT_Magnitude[k];
				if ( Sig > Sig_Max) Sig_Max = Sig;
				Sig_Sum1 += Sig;
			}
			Sig_Sum0 = Sig_Max;
		}
		if ( j > 8 && j < 128 )
		{
			Sig_Sum2 += (int)FFT_Magnitude[j];
			number_bins++;
		}
	}
	switch (AGC_Mode){
		case 0:
			Sig_Total = Sig_Sum0;
			break;
		case 1:
			Sig_Total = Sig_Sum1;
			break;
		case 2:
			Sig_Total = Sig_Sum2;
			break;
	}
	RMS_Sig = 10*sqrt((float32_t)Sig_Total);
	dB_Sig = 23. + 10*log((float32_t)Sig_Total + .001);

	AGC_Mag = FFT_Coeff*RMS_Sig + (1.-FFT_Coeff)*AGC_Mag;
	RSL_Mag = FFT_Coeff*dB_Sig + (1.-FFT_Coeff)*RSL_Mag;

}//End of Process_FFT


