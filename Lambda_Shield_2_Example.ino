/*
    Example code compatible with the Lambda Shield for Arduino.
    
    Copyright (C) 2017 - 2020 Bylund Automotive AB.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    Contact information of author:
    http://www.bylund-automotive.com/
    
    info@bylund-automotive.com

    Version history:
    2020-03-29        v1.0.0        First release to GitHub.
    2020-06-18        v1.1.0        Implemented support for data logging.

*/

//Define included headers.
#include <SPI.h>
#include <SD.h>

//Define CJ125 registers used.
#define           CJ125_IDENT_REG_REQUEST             0x4800        /* Identify request, gives revision of the chip. */
#define           CJ125_DIAG_REG_REQUEST              0x7800        /* Dignostic request, gives the current status. */
#define           CJ125_INIT_REG1_REQUEST             0x6C00        /* Requests the first init register. */
#define           CJ125_INIT_REG2_REQUEST             0x7E00        /* Requests the second init register. */
#define           CJ125_INIT_REG1_MODE_CALIBRATE      0x569D        /* Sets the first init register in calibration mode. */
#define           CJ125_INIT_REG1_MODE_NORMAL_V8      0x5688        /* Sets the first init register in operation mode. V=8 amplification. */
#define           CJ125_INIT_REG1_MODE_NORMAL_V17     0x5689        /* Sets the first init register in operation mode. V=17 amplification. */
#define           CJ125_DIAG_REG_STATUS_OK            0x28FF        /* The response of the diagnostic register when everything is ok. */
#define           CJ125_DIAG_REG_STATUS_NOPOWER       0x2855        /* The response of the diagnostic register when power is low. */
#define           CJ125_DIAG_REG_STATUS_NOSENSOR      0x287F        /* The response of the diagnostic register when no sensor is connected. */
#define           CJ125_INIT_REG1_STATUS_0            0x2888        /* The response of the init register when V=8 amplification is in use. */
#define           CJ125_INIT_REG1_STATUS_1            0x2889        /* The response of the init register when V=17 amplification is in use. */

//Define pin assignments.
#define           CJ125_NSS_PIN                       10            /* Pin used for chip select in SPI communication. */
#define           LED_STATUS_POWER                    7             /* Pin used for power the status LED, indicating we have power. */
#define           LED_STATUS_HEATER                   6             /* Pin used for the heater status LED, indicating heater activity. */
#define           HEATER_OUTPUT_PIN                   5             /* Pin used for the PWM output to the heater circuit. */
#define           ANALOG_OUTPUT_PIN                   3             /* Pin used for the PWM to the 0-1V analog output. */
#define           UB_ANALOG_INPUT_PIN                 2             /* Analog input for power supply.*/
#define           UR_ANALOG_INPUT_PIN                 1             /* Analog input for temperature.*/
#define           UA_ANALOG_INPUT_PIN                 0             /* Analog input for lambda.*/

//Define adjustable parameters.
#define           SERIAL_RATE                         1             /* Serial refresh rate in HZ (1-100)*/                           
#define           UBAT_MIN                            150           /* Minimum voltage (ADC value) on Ubat to operate */

//Global variables.
int adcValue_UA = 0;                                                /* ADC value read from the CJ125 UA output pin */ 
int adcValue_UR = 0;                                                /* ADC value read from the CJ125 UR output pin */
int adcValue_UB = 0;                                                /* ADC value read from the voltage divider caluclating Ubat */
int adcValue_UA_Optimal = 0;                                        /* UA ADC value stored when CJ125 is in calibration mode, λ=1 */ 
int adcValue_UR_Optimal = 0;                                        /* UR ADC value stored when CJ125 is in calibration mode, optimal temperature */
int HeaterOutput = 0;                                               /* Current PWM output value (0-255) of the heater output pin */
int serial_counter = 0;                                             /* Counter used to calculate refresh rate on the serial output */
int CJ125_Status = 0;                                               /* Latest stored DIAG registry response from the CJ125 */
bool logEnabled = false;                                            /* Variable used for setting data logging enable or disabled. */


//PID regulation variables.
int dState;                                                         /* Last position input. */
int iState;                                                         /* Integrator state. */
const int iMax = 250;                                               /* Maximum allowable integrator state. */
const int iMin = -250;                                              /* Minimum allowable integrator state. */
const float pGain = 120;                                            /* Proportional gain. Default = 120*/
const float iGain = 0.8;                                            /* Integral gain. Default = 0.8*/
const float dGain = 10;                                             /* Derivative gain. Default = 10*/

//Lambda Conversion Lookup Table. (ADC 39-791).
const PROGMEM float Lambda_Conversion[753] {
  0.750, 0.751, 0.752, 0.752, 0.753, 0.754, 0.755, 0.755, 0.756, 0.757, 0.758, 0.758, 0.759, 0.760, 0.761, 0.761, 0.762, 0.763, 0.764, 0.764,
  0.765, 0.766, 0.766, 0.767, 0.768, 0.769, 0.769, 0.770, 0.771, 0.772, 0.772, 0.773, 0.774, 0.774, 0.775, 0.776, 0.777, 0.777, 0.778, 0.779,
  0.780, 0.780, 0.781, 0.782, 0.782, 0.783, 0.784, 0.785, 0.785, 0.786, 0.787, 0.787, 0.788, 0.789, 0.790, 0.790, 0.791, 0.792, 0.793, 0.793,
  0.794, 0.795, 0.796, 0.796, 0.797, 0.798, 0.799, 0.799, 0.800, 0.801, 0.802, 0.802, 0.803, 0.804, 0.805, 0.805, 0.806, 0.807, 0.808, 0.808,
  0.809, 0.810, 0.811, 0.811, 0.812, 0.813, 0.814, 0.815, 0.815, 0.816, 0.817, 0.818, 0.819, 0.820, 0.820, 0.821, 0.822, 0.823, 0.824, 0.825,
  0.825, 0.826, 0.827, 0.828, 0.829, 0.830, 0.830, 0.831, 0.832, 0.833, 0.834, 0.835, 0.836, 0.837, 0.837, 0.838, 0.839, 0.840, 0.841, 0.842,
  0.843, 0.844, 0.845, 0.846, 0.846, 0.847, 0.848, 0.849, 0.850, 0.851, 0.852, 0.853, 0.854, 0.855, 0.855, 0.856, 0.857, 0.858, 0.859, 0.860,
  0.861, 0.862, 0.863, 0.864, 0.865, 0.865, 0.866, 0.867, 0.868, 0.869, 0.870, 0.871, 0.872, 0.873, 0.874, 0.875, 0.876, 0.877, 0.878, 0.878,
  0.879, 0.880, 0.881, 0.882, 0.883, 0.884, 0.885, 0.886, 0.887, 0.888, 0.889, 0.890, 0.891, 0.892, 0.893, 0.894, 0.895, 0.896, 0.897, 0.898,
  0.899, 0.900, 0.901, 0.902, 0.903, 0.904, 0.905, 0.906, 0.907, 0.908, 0.909, 0.910, 0.911, 0.912, 0.913, 0.915, 0.916, 0.917, 0.918, 0.919,
  0.920, 0.921, 0.922, 0.923, 0.924, 0.925, 0.926, 0.927, 0.928, 0.929, 0.931, 0.932, 0.933, 0.934, 0.935, 0.936, 0.937, 0.938, 0.939, 0.940,
  0.941, 0.942, 0.944, 0.945, 0.946, 0.947, 0.948, 0.949, 0.950, 0.951, 0.952, 0.953, 0.954, 0.955, 0.957, 0.958, 0.959, 0.960, 0.961, 0.962,
  0.963, 0.965, 0.966, 0.967, 0.969, 0.970, 0.971, 0.973, 0.974, 0.976, 0.977, 0.979, 0.980, 0.982, 0.983, 0.985, 0.986, 0.987, 0.989, 0.990,
  0.991, 0.992, 0.994, 0.995, 0.996, 0.998, 0.999, 1.001, 1.003, 1.005, 1.008, 1.010, 1.012, 1.015, 1.017, 1.019, 1.022, 1.024, 1.026, 1.028,
  1.030, 1.032, 1.035, 1.037, 1.039, 1.041, 1.043, 1.045, 1.048, 1.050, 1.052, 1.055, 1.057, 1.060, 1.062, 1.064, 1.067, 1.069, 1.072, 1.075,
  1.077, 1.080, 1.082, 1.085, 1.087, 1.090, 1.092, 1.095, 1.098, 1.100, 1.102, 1.105, 1.107, 1.110, 1.112, 1.115, 1.117, 1.120, 1.122, 1.124,
  1.127, 1.129, 1.132, 1.135, 1.137, 1.140, 1.142, 1.145, 1.148, 1.151, 1.153, 1.156, 1.159, 1.162, 1.165, 1.167, 1.170, 1.173, 1.176, 1.179,
  1.182, 1.185, 1.188, 1.191, 1.194, 1.197, 1.200, 1.203, 1.206, 1.209, 1.212, 1.215, 1.218, 1.221, 1.224, 1.227, 1.230, 1.234, 1.237, 1.240,
  1.243, 1.246, 1.250, 1.253, 1.256, 1.259, 1.262, 1.266, 1.269, 1.272, 1.276, 1.279, 1.282, 1.286, 1.289, 1.292, 1.296, 1.299, 1.303, 1.306,
  1.310, 1.313, 1.317, 1.320, 1.324, 1.327, 1.331, 1.334, 1.338, 1.342, 1.345, 1.349, 1.352, 1.356, 1.360, 1.364, 1.367, 1.371, 1.375, 1.379,
  1.382, 1.386, 1.390, 1.394, 1.398, 1.401, 1.405, 1.409, 1.413, 1.417, 1.421, 1.425, 1.429, 1.433, 1.437, 1.441, 1.445, 1.449, 1.453, 1.457,
  1.462, 1.466, 1.470, 1.474, 1.478, 1.483, 1.487, 1.491, 1.495, 1.500, 1.504, 1.508, 1.513, 1.517, 1.522, 1.526, 1.531, 1.535, 1.540, 1.544,
  1.549, 1.554, 1.558, 1.563, 1.568, 1.572, 1.577, 1.582, 1.587, 1.592, 1.597, 1.601, 1.606, 1.611, 1.616, 1.621, 1.627, 1.632, 1.637, 1.642,
  1.647, 1.652, 1.658, 1.663, 1.668, 1.674, 1.679, 1.684, 1.690, 1.695, 1.701, 1.707, 1.712, 1.718, 1.724, 1.729, 1.735, 1.741, 1.747, 1.753,
  1.759, 1.764, 1.770, 1.776, 1.783, 1.789, 1.795, 1.801, 1.807, 1.813, 1.820, 1.826, 1.832, 1.839, 1.845, 1.852, 1.858, 1.865, 1.872, 1.878,
  1.885, 1.892, 1.898, 1.905, 1.912, 1.919, 1.926, 1.933, 1.940, 1.947, 1.954, 1.961, 1.968, 1.975, 1.983, 1.990, 1.997, 2.005, 2.012, 2.020,
  2.027, 2.035, 2.042, 2.050, 2.058, 2.065, 2.073, 2.081, 2.089, 2.097, 2.105, 2.113, 2.121, 2.129, 2.137, 2.145, 2.154, 2.162, 2.171, 2.179,
  2.188, 2.196, 2.205, 2.214, 2.222, 2.231, 2.240, 2.249, 2.258, 2.268, 2.277, 2.286, 2.295, 2.305, 2.314, 2.324, 2.333, 2.343, 2.353, 2.363,
  2.373, 2.383, 2.393, 2.403, 2.413, 2.424, 2.434, 2.444, 2.455, 2.466, 2.476, 2.487, 2.498, 2.509, 2.520, 2.532, 2.543, 2.554, 2.566, 2.577,
  2.589, 2.601, 2.613, 2.625, 2.637, 2.649, 2.662, 2.674, 2.687, 2.699, 2.712, 2.725, 2.738, 2.751, 2.764, 2.778, 2.791, 2.805, 2.819, 2.833,
  2.847, 2.861, 2.875, 2.890, 2.904, 2.919, 2.934, 2.949, 2.964, 2.979, 2.995, 3.010, 3.026, 3.042, 3.058, 3.074, 3.091, 3.107, 3.124, 3.141,
  3.158, 3.175, 3.192, 3.209, 3.227, 3.245, 3.263, 3.281, 3.299, 3.318, 3.337, 3.355, 3.374, 3.394, 3.413, 3.433, 3.452, 3.472, 3.492, 3.513,
  3.533, 3.554, 3.575, 3.597, 3.618, 3.640, 3.662, 3.684, 3.707, 3.730, 3.753, 3.776, 3.800, 3.824, 3.849, 3.873, 3.898, 3.924, 3.950, 3.976,
  4.002, 4.029, 4.056, 4.084, 4.112, 4.140, 4.169, 4.198, 4.228, 4.258, 4.288, 4.319, 4.350, 4.382, 4.414, 4.447, 4.480, 4.514, 4.548, 4.583,
  4.618, 4.654, 4.690, 4.726, 4.764, 4.801, 4.840, 4.879, 4.918, 4.958, 4.999, 5.040, 5.082, 5.124, 5.167, 5.211, 5.255, 5.299, 5.345, 5.391,
  5.438, 5.485, 5.533, 5.582, 5.632, 5.683 ,5.735, 5.788, 5.841, 5.896, 5.953, 6.010, 6.069, 6.129, 6.190, 6.253, 6.318, 6.384, 6.452, 6.521,
  6.592, 6.665, 6.740, 6.817, 6.896, 6.976, 7.059, 7.144, 7.231, 7.320, 7.412, 7.506, 7.602, 7.701, 7.803, 7.906, 8.013, 8.122, 8.234, 8.349,
  8.466, 8.587, 8.710, 8.837, 8.966, 9.099, 9.235, 9.374, 9.516, 9.662, 9.811, 9.963, 10.119
};

//Oxygen Conversion Lookup Table. (ADC 307-854).
const PROGMEM float Oxygen_Conversion[548] {
  00.00, 00.04, 00.08, 00.13, 00.17, 00.21, 00.25, 00.30, 00.34, 00.38, 00.42, 00.47, 00.51, 00.55, 00.59, 00.64, 00.68, 00.72, 00.76, 00.81,
  00.85, 00.89, 00.93, 00.98, 01.02, 01.06, 01.10, 01.15, 01.19, 01.23, 01.27, 01.31, 01.36, 01.40, 01.44, 01.48, 01.53, 01.57, 01.61, 01.65,
  01.70, 01.74, 01.78, 01.82, 01.86, 01.91, 01.95, 01.99, 02.03, 02.08, 02.12, 02.16, 02.20, 02.24, 02.29, 02.33, 02.37, 02.41, 02.45, 02.50,
  02.54, 02.58, 02.62, 02.66, 02.71, 02.75, 02.79, 02.83, 02.87, 02.92, 02.96, 03.00, 03.04, 03.08, 03.13, 03.17, 03.21, 03.25, 03.29, 03.33,
  03.38, 03.42, 03.46, 03.50, 03.54, 03.58, 03.63, 03.67, 03.71, 03.75, 03.79, 03.83, 03.88, 03.92, 03.96, 04.00, 04.04, 04.08, 04.12, 04.17,
  04.21, 04.25, 04.29, 04.33, 04.37, 04.41, 04.45, 04.50, 04.54, 04.58, 04.62, 04.66, 04.70, 04.74, 04.78, 04.82, 04.86, 04.91, 04.95, 04.99,
  05.03, 05.07, 05.11, 05.15, 05.19, 05.23, 05.27, 05.31, 05.35, 05.39, 05.44, 05.48, 05.52, 05.56, 05.60, 05.64, 05.68, 05.72, 05.76, 05.80,
  05.84, 05.88, 05.92, 05.96, 06.00, 06.04, 06.08, 06.12, 06.16, 06.20, 06.24, 06.28, 06.32, 06.36, 06.40, 06.44, 06.48, 06.52, 06.56, 06.60,
  06.64, 06.68, 06.72, 06.76, 06.80, 06.84, 06.88, 06.92, 06.96, 07.00, 07.03, 07.07, 07.11, 07.15, 07.19, 07.23, 07.27, 07.31, 07.35, 07.39,
  07.43, 07.47, 07.51, 07.55, 07.59, 07.62, 07.66, 07.70, 07.74, 07.78, 07.82, 07.86, 07.90, 07.94, 07.98, 08.02, 08.06, 08.09, 08.13, 08.17,
  08.21, 08.25, 08.29, 08.33, 08.37, 08.41, 08.45, 08.49, 08.52, 08.56, 08.60, 08.64, 08.68, 08.72, 08.76, 08.80, 08.84, 08.88, 08.91, 08.95,
  08.99, 09.03, 09.07, 09.11, 09.15, 09.19, 09.23, 09.26, 09.30, 09.34, 09.38, 09.42, 09.46, 09.50, 09.54, 09.57, 09.61, 09.65, 09.69, 09.73,
  09.77, 09.81, 09.85, 09.89, 09.92, 09.96, 10.00, 10.04, 10.08, 10.12, 10.16, 10.19, 10.23, 10.27, 10.31, 10.35, 10.39, 10.43, 10.47, 10.50,
  10.54, 10.58, 10.62, 10.66, 10.70, 10.73, 10.77, 10.81, 10.85, 10.89, 10.93, 10.97, 11.00, 11.04, 11.08, 11.12, 11.16, 11.20, 11.23, 11.27,
  11.31, 11.35, 11.39, 11.43, 11.46, 11.50, 11.54, 11.58, 11.62, 11.66, 11.69, 11.73, 11.77, 11.81, 11.85, 11.89, 11.92, 11.96, 12.00, 12.04,
  12.08, 12.11, 12.15, 12.19, 12.23, 12.27, 12.30, 12.34, 12.38, 12.42, 12.46, 12.49, 12.53, 12.57, 12.61, 12.65, 12.68, 12.72, 12.76, 12.80,
  12.84, 12.87, 12.91, 12.95, 12.99, 13.03, 13.06, 13.10, 13.14, 13.18, 13.21, 13.25, 13.29, 13.33, 13.36, 13.40, 13.44, 13.48, 13.51, 13.55,
  13.59, 13.63, 13.67, 13.70, 13.74, 13.78, 13.82, 13.85, 13.89, 13.93, 13.96, 14.00, 14.04, 14.08, 14.11, 14.15, 14.19, 14.23, 14.26, 14.30,
  14.34, 14.38, 14.41, 14.45, 14.49, 14.52, 14.56, 14.60, 14.64, 14.67, 14.71, 14.75, 14.78, 14.82, 14.86, 14.90, 14.93, 14.97, 15.01, 15.04,
  15.08, 15.12, 15.15, 15.19, 15.23, 15.26, 15.30, 15.34, 15.37, 15.41, 15.45, 15.48, 15.52, 15.56, 15.59, 15.63, 15.67, 15.70, 15.74, 15.78,
  15.81, 15.85, 15.89, 15.92, 15.96, 16.00, 16.03, 16.07, 16.11, 16.14, 16.18, 16.22, 16.25, 16.29, 16.32, 16.36, 16.40, 16.43, 16.47, 16.51,
  16.54, 16.58, 16.61, 16.65, 16.69, 16.72, 16.76, 16.79, 16.83, 16.87, 16.90, 16.94, 16.97, 17.01, 17.05, 17.08, 17.12, 17.15, 17.19, 17.22,
  17.26, 17.30, 17.33, 17.37, 17.40, 17.44, 17.47, 17.51, 17.55, 17.58, 17.62, 17.65, 17.69, 17.72, 17.76, 17.79, 17.83, 17.86, 17.90, 17.94,
  17.97, 18.01, 18.04, 18.08, 18.11, 18.15, 18.18, 18.22, 18.25, 18.29, 18.32, 18.36, 18.39, 18.43, 18.46, 18.50, 18.53, 18.57, 18.60, 18.64,
  18.67, 18.71, 18.74, 18.78, 18.81, 18.85, 18.88, 18.92, 18.95, 18.98, 19.02, 19.05, 19.09, 19.12, 19.16, 19.19, 19.23, 19.26, 19.30, 19.33,
  19.36, 19.40, 19.43, 19.47, 19.50, 19.54, 19.57, 19.60, 19.64, 19.67, 19.71, 19.74, 19.77, 19.81, 19.84, 19.88, 19.91, 19.94, 19.98, 20.01,
  20.05, 20.08, 20.11, 20.15, 20.18, 20.22, 20.25, 20.28, 20.32, 20.35, 20.38, 20.42, 20.45, 20.48, 20.52, 20.55, 20.58, 20.62, 20.65, 20.68,
  20.72, 20.75, 20.78, 20.82, 20.85, 20.88, 20.92, 20.95
};

//Function for transfering SPI data to the CJ125.
uint16_t COM_SPI(uint16_t TX_data) {

  //Configure SPI for CJ125 controller.
  SPI.setDataMode(SPI_MODE1);
  SPI.setClockDivider(SPI_CLOCK_DIV128);
  
  //Set chip select pin low, chip in use. 
  digitalWrite(CJ125_NSS_PIN, LOW);

  //Transmit request.
  uint16_t Response =  SPI.transfer16(TX_data);

  //Set chip select pin high, chip not in use.
  digitalWrite(CJ125_NSS_PIN, HIGH);

  return Response;
  
}

//Temperature regulating software (PID).
int Heater_PID_Control(int input) {
  
  //Calculate error term.
  int error = adcValue_UR_Optimal - input;
  
  //Set current position.
  int position = input;
  
  //Calculate proportional term.
  float pTerm = -pGain * error;
  
  //Calculate the integral state with appropriate limiting.
  iState += error;
  
  if (iState > iMax) iState = iMax;
  if (iState < iMin) iState = iMin;
  
  //Calculate the integral term.
  float iTerm = -iGain * iState;
  
  //Calculate the derivative term.
  float dTerm = -dGain * (dState - position);
  dState = position;
  
  //Calculate regulation (PI).
  int RegulationOutput = pTerm + iTerm + dTerm;
  
  //Set maximum heater output (full power).
  if (RegulationOutput > 255) RegulationOutput = 255;
  
  //Set minimum heater value (cooling).
  if (RegulationOutput < 0.0) RegulationOutput = 0;

  //Return calculated PWM output.
  return RegulationOutput;
  
}



//Lookup Lambda Value.
float Lookup_Lambda(int Input_ADC) {
  
    //Declare and set default return value.
    float LAMBDA_VALUE = 0;

    //Validate ADC range for lookup table.
    if (Input_ADC >= 39 && Input_ADC <= 791) {
      LAMBDA_VALUE = pgm_read_float_near(Lambda_Conversion + (Input_ADC-39));
    }
    
    if (Input_ADC > 791) {
      LAMBDA_VALUE = 10.119;
    }

    if (Input_ADC < 39) {
      LAMBDA_VALUE = 0.750;
    }
    
    //Return value.
    return LAMBDA_VALUE;
    
}

//Lookup Oxygen Content.
float Lookup_Oxygen(int Input_ADC) {
  
    //Declare and set default return value.
    float OXYGEN_CONTENT = 0;

    //Validate ADC range for lookup table.
    if (Input_ADC > 854) Input_ADC = 854;
    
    if (Input_ADC >= 307 && Input_ADC <= 854) {
      OXYGEN_CONTENT = pgm_read_float_near(Oxygen_Conversion + (Input_ADC - 307));
    }
    
    //Return value.
    return OXYGEN_CONTENT;
    
}

//Data logging function.
void logData(String logString) {

  //Connect to SD-Card.
  if ( SD.begin() ) {

    //Open file.
    File logFile = SD.open("log.txt", FILE_WRITE);

    //Store data.
    logFile.println(logString);

    //Close file.
    logFile.close();

    //Flush SPI, required when switching between modes.
    COM_SPI(0x00);
    
  } else {
    
    //Error handling.
    Serial.println("Error accessing SD-card.");  
    
  }
  
}

//Function to set up device for operation.
void setup() {
  
  //Set up serial communication.
  Serial.begin(9600);

  //Set up SPI.
  SPI.begin();  /* Note, SPI will disable the bult in LED. */
  SPI.setBitOrder(MSBFIRST);

  //Set up digital output pins.
  pinMode(CJ125_NSS_PIN, OUTPUT);  
  pinMode(LED_STATUS_POWER, OUTPUT);
  pinMode(LED_STATUS_HEATER, OUTPUT);
  pinMode(HEATER_OUTPUT_PIN, OUTPUT);

  //Set initial values.
  digitalWrite(CJ125_NSS_PIN, HIGH);
  digitalWrite(LED_STATUS_POWER, LOW);
  digitalWrite(LED_STATUS_HEATER, LOW);
  analogWrite(HEATER_OUTPUT_PIN, 0); /* PWM is initially off. */
  analogWrite(ANALOG_OUTPUT_PIN, 0); /* PWM is initially off. */
    
  //Start of operation. (Test LED's).
  Serial.print("Device reset.\n\r");
  digitalWrite(LED_STATUS_POWER, HIGH);
  digitalWrite(LED_STATUS_HEATER, HIGH);
  delay(200);
  digitalWrite(LED_STATUS_POWER, LOW);
  digitalWrite(LED_STATUS_HEATER, LOW);

  //Configure data logging.
  if ( SD.begin() ) {
    
    //Enable data logging.
    Serial.println("Data logging enabled.");
    logEnabled = true;

    //Flush SPI, required when switching between modes.
    COM_SPI(0x00);
    
  }
  
  //Start main function.
  start();

}

void start() {
  
  //Wait until everything is ready.
  while (adcValue_UB < UBAT_MIN || CJ125_Status != CJ125_DIAG_REG_STATUS_OK) {
    
    //Read CJ125 diagnostic register from SPI.
    CJ125_Status = COM_SPI(CJ125_DIAG_REG_REQUEST);

    //Error handling.
    if (CJ125_Status != CJ125_DIAG_REG_STATUS_OK) {
      Serial.print("Error, CJ125: 0x");
      Serial.print(CJ125_Status, HEX);
      Serial.print("\n\r");
    }
    
    //Read input voltage.
    adcValue_UB = analogRead(UB_ANALOG_INPUT_PIN);

    delay(1000);
  }

  //Start of operation. (Start Power LED).
  Serial.print("Device ready.\n\r");
  digitalWrite(LED_STATUS_POWER, HIGH);

  //Store calibrated optimum values.
  Serial.print("Reading calibration data.\n\r");

  //Set CJ125 in calibration mode.
  COM_SPI(CJ125_INIT_REG1_MODE_CALIBRATE);

  //Let values settle.
  delay(500);

  //Store optimal values before leaving calibration mode.
  adcValue_UA_Optimal = analogRead(UA_ANALOG_INPUT_PIN);
  adcValue_UR_Optimal = analogRead(UR_ANALOG_INPUT_PIN);
    
  //Set CJ125 in normal operation mode.
  //COM_SPI(CJ125_INIT_REG1_MODE_NORMAL_V8);  /* V=0 */
  COM_SPI(CJ125_INIT_REG1_MODE_NORMAL_V17);  /* V=1 */

  //Present calibration data:
  Serial.print("UA_Optimal (λ = 1.00): ");
  Serial.print(adcValue_UA_Optimal);
  Serial.print(" (λ = ");
  Serial.print(Lookup_Lambda(adcValue_UA_Optimal), 2);
  Serial.print(")\n\r");
  Serial.print("UR_Optimal: ");
  Serial.print(adcValue_UR_Optimal);
  Serial.print("\n\r");
  
  /* Heat up sensor. This is described in detail in the datasheet of the LSU 4.9 sensor with a 
   * condensation phase and a ramp up face before going in to PID control. */
  Serial.print("Heating sensor.\n\r");    

  //Calculate supply voltage.
  float SupplyVoltage = (((float)adcValue_UB / 1023 * 5) / 10000) * 110000;

  //Condensation phase, 2V for 5s.
  int CondensationPWM = (2 / SupplyVoltage) * 255;
  analogWrite(HEATER_OUTPUT_PIN, CondensationPWM);

  int t = 0;
  while (t < 5 && analogRead(UB_ANALOG_INPUT_PIN) > UBAT_MIN) {

    //Flash Heater LED in condensation phase.
    digitalWrite(LED_STATUS_HEATER, HIGH);  
    delay(500);
          
    digitalWrite(LED_STATUS_HEATER, LOW);
    delay(500);

    t += 1;
    
  }

  //Ramp up phase, +0.4V / s until 100% PWM from 8.5V.
  float UHeater = 8.5;
  while (UHeater < 13.0 && analogRead(UB_ANALOG_INPUT_PIN) > UBAT_MIN) {

    //Set heater output during ramp up.
    CondensationPWM = (UHeater / SupplyVoltage) * 255;
      
    if (CondensationPWM > 255) CondensationPWM = 255; /*If supply voltage is less than 13V, maximum is 100% PWM*/

    analogWrite(HEATER_OUTPUT_PIN, CondensationPWM);

    //Flash Heater LED in condensation phase.
    digitalWrite(LED_STATUS_HEATER, HIGH);
    delay(500);
      
    digitalWrite(LED_STATUS_HEATER, LOW);
    delay(500);

    //Increment Voltage.
    UHeater += 0.4;
      
  }

  //Heat until temperature optimum is reached or exceeded (lower value is warmer).
  while (analogRead(UR_ANALOG_INPUT_PIN) > adcValue_UR_Optimal && analogRead(UB_ANALOG_INPUT_PIN) > UBAT_MIN) {

    //Flash Heater LED in condensation phase.
    digitalWrite(LED_STATUS_HEATER, HIGH);
    delay(500);
      
    digitalWrite(LED_STATUS_HEATER, LOW);
    delay(500);

  }

  //Heating phase finished, hand over to PID-control. Turn on LED and turn off heater.
  digitalWrite(LED_STATUS_HEATER, HIGH);
  analogWrite(HEATER_OUTPUT_PIN, 0);
  
}

//Infinite loop.
void loop() {

  //Update CJ125 diagnostic register from SPI.
  CJ125_Status = COM_SPI(CJ125_DIAG_REG_REQUEST);

  //Update analog inputs.
  adcValue_UA = analogRead(UA_ANALOG_INPUT_PIN);
  adcValue_UR = analogRead(UR_ANALOG_INPUT_PIN);
  adcValue_UB = analogRead(UB_ANALOG_INPUT_PIN);

  //Adjust PWM output by calculated PID regulation.
  if (adcValue_UR < 500 || adcValue_UR_Optimal != 0 || adcValue_UB > UBAT_MIN) {
    
    //Calculate and set new heater output.
    HeaterOutput = Heater_PID_Control(adcValue_UR);
    analogWrite(HEATER_OUTPUT_PIN, HeaterOutput);
    
  } else {
    
    //Turn off heater if we are not in PID control.
    HeaterOutput = 0;
    analogWrite(HEATER_OUTPUT_PIN, HeaterOutput);
    
  }

  //If power is lost, "reset" the device.
  if (adcValue_UB < UBAT_MIN) {

    //Indicate low power.
    Serial.print("Low power.\n");

    //Turn of status LEDs.
    digitalWrite(LED_STATUS_POWER, LOW);
    digitalWrite(LED_STATUS_HEATER, LOW);

    //Re-start() and wait for power.
    start();
    
  }
  
  //Display on serial port at defined rate. Comma separate values, readable by frontends.
  if ( (100 / SERIAL_RATE) ==  serial_counter) {

    //Reset counter.
    serial_counter = 0;

    //Calculate Lambda Value.
    float LAMBDA_VALUE = Lookup_Lambda(adcValue_UA);
      
    //Calculate Oxygen Content.
    float OXYGEN_CONTENT = Lookup_Oxygen(adcValue_UA);

    //Display information if no errors is reported.
    if (CJ125_Status == CJ125_DIAG_REG_STATUS_OK) {
      
      //Assembled data.
      String txString = "Measuring, CJ125: 0x";
      txString += String(CJ125_Status, HEX);
      txString += ", UA_ADC: ";
      txString += String(adcValue_UA, DEC);
      txString += ", UR_ADC: ";
      txString += String(adcValue_UR, DEC);
      txString += ", UB_ADC: ";
      txString += String(adcValue_UB, DEC);

      //Display lambda value unless out of range.
      if (adcValue_UA >= 39 && adcValue_UA <= 791) {
          txString += ", Lambda: ";
          txString += String(LAMBDA_VALUE, 2);
      } else {
          txString += ", Lambda: -";
      }

      //Display oxygen unless out of range.
      if (adcValue_UA >= 307) {
        txString += ", Oxygen: ";
        txString += String(OXYGEN_CONTENT, 2);
        txString += "%";
      } else {
        txString += ", Oxygen: -";
      }
      
      //Output string
      Serial.println(txString);

      //Log string.
      if (logEnabled == true) logData(txString);
      
    } else {
      
      //Error handling.
      switch(CJ125_Status) {

        case CJ125_DIAG_REG_STATUS_NOPOWER:
          Serial.print("Error, CJ125: 0x");
          Serial.print(CJ125_Status, HEX);
          Serial.print(" (No Power)\n\r");
        break;
  
        case CJ125_DIAG_REG_STATUS_NOSENSOR:
          Serial.print("Error, CJ125: 0x");
          Serial.print(CJ125_Status, HEX);
          Serial.print(" (No Sensor)\n\r");
        break;

        default:
          Serial.print("Error, CJ125: 0x");
          Serial.print(CJ125_Status, HEX);
          Serial.print("\n\r");
        
        }
        
    }

  }

  //Increment serial output counter and delay for next cycle. The PID requires to be responsive but we don't need to flood the serial port.
  serial_counter++;
  delay(10);

}
