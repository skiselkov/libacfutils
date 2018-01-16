
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include "GeomagnetismLibrary.h"

/* Ignore unused result - 3rd party library which we do not control */
#pragma GCC diagnostic ignored "-Wunused-result"

/* $Id: GeomagnetismLibrary.c 1287 2014-12-09 22:55:09Z awoods $
 *
 * ABSTRACT
 *
 * The purpose of Geomagnetism Library is primarily to support the World Magnetic Model (WMM) 2015-2020.
 * It however is built to be used for spherical harmonic models of the Earth's magnetic field
 * generally and supports models even with a large (>>12) number of degrees.  It is also used in many
 * other geomagnetic models distributed by NGDC.
 *
 * REUSE NOTES
 *
 * Geomagnetism Library is intended for reuse by any application that requires
 * Computation of Geomagnetic field from a spherical harmonic model.
 *
 * REFERENCES
 *
 *    Further information on Geoid can be found in the WMM Technical Documents.
 *
 *
 * LICENSES
 *
 *  The WMM source code is in the public domain and not licensed or under copyright.
 *	The information and software may be used freely by the public. As required by 17 U.S.C. 403,
 *	third parties producing copyrighted works consisting predominantly of the material produced by
 *	U.S. government agencies must provide notice with such work(s) identifying the U.S. Government material
 *	incorporated and stating that such material is not subject to copyright protection.
 *
 * RESTRICTIONS
 *
 *    Geomagnetism library has no restrictions.
 *
 * ENVIRONMENT
 *
 *    Geomagnetism library was tested in the following environments
 *
 *    1. Red Hat Linux  with GCC Compiler
 *    2. MS Windows 7 with MinGW compiler
 *    3. Sun Solaris with GCC Compiler
 *
 *

 *  National Geophysical Data Center
 *  NOAA EGC/2
 *  325 Broadway
 *  Boulder, CO 80303 USA
 *  Attn: Susan McLean
 *  Phone:  (303) 497-6478
 *  Email:  Susan.McLean@noaa.gov

 *  Software and Model Support
 *  National Geophysical Data Center
 *  NOAA EGC/2
 *  325 Broadway"
 *  Boulder, CO 80303 USA
 *  Attn: Manoj Nair or Arnaud Chulliat
 *  Phone:  (303) 497-4642 or -6522
 *  Email:  geomag.models@noaa.gov
 *  URL: http://www.ngdc.noaa.gov/Geomagnetic/WMM/DoDWMM.shtml

 *  For more details on the subroutines, please consult the WMM
 *  Technical Documentations at
 *  http://www.ngdc.noaa.gov/Geomagnetic/WMM/DoDWMM.shtml

 *  Nov 23, 2009
 *  Written by Manoj C Nair and Adam Woods
 *  Manoj.C.Nair@Noaa.Gov
 *  Revision Number: $Revision: 1287 $
 *  Last changed by: $Author: awoods $
 *  Last changed on: $Date: 2014-12-09 15:55:09 -0700 (Tue, 09 Dec 2014) $
 */

enum PARAMS {
	SHDF,
	MODELNAME,
	PUBLISHER,
	RELEASEDATE,
	DATACUTOFF,
	MODELSTARTYEAR,
	MODELENDYEAR,
	EPOCH,
	INTSTATICDEG,
	INTSECVARDEG,
	EXTSTATICDEG,
	EXTSECVARDEG,
	GEOMAGREFRAD,
	NORMALIZATION,
	SPATBASFUNC
};

typedef struct {
	double *Pcup;		/* Legendre Function */
	double *dPcup;		/* Derivative of Legendre fcn */
} MAGtype_LegendreFunction;

typedef struct {
	double Bx;		/* North */
	double By;		/* East */
	double Bz;		/* Down */
} MAGtype_MagneticResults;

typedef struct {
	/* [earth_reference_radius_km / sph. radius ]^n  */
	double *RelativeRadiusPower;
	/*cp(m)  - cosine of (m*spherical coord. longitude) */
	double *cos_mlambda;
	/* sp(m)  - sine of (m*spherical coord. longitude) */
	double *sin_mlambda;
} MAGtype_SphericalHarmonicVariables;

static char *MAG_Trim(char *str);
static int MAG_readMagneticModel_SHDF(const char *filename,
    MAGtype_MagneticModel **magneticmodel);

static MAGtype_LegendreFunction *MAG_AllocateLegendreFunctionMemory(
    int NumTerms);

static MAGtype_SphericalHarmonicVariables *MAG_AllocateSphVarMemory(int nMax);

static void MAG_AssignHeaderValues(MAGtype_MagneticModel *model,
    char values[][MAXLINELENGTH]);

static int MAG_FreeLegendreMemory(MAGtype_LegendreFunction *LegendreFunction);
static int MAG_FreeSphVarMemory(MAGtype_SphericalHarmonicVariables *SphVar);

/*Conversions, Transformations, and other Calculations*/
static int MAG_CalculateGeoMagneticElements(MAGtype_MagneticResults *
    MagneticResultsGeo, MAGtype_GeoMagneticElements * GeoMagneticElements);

static int MAG_CalculateSecularVariationElements(MAGtype_MagneticResults
    MagneticVariation, MAGtype_GeoMagneticElements * MagneticElements);

static void MAG_CartesianToGeodetic(MAGtype_Ellipsoid Ellip, double x, double y,
    double z, MAGtype_CoordGeodetic * CoordGeodetic);

static int MAG_RotateMagneticVector(MAGtype_CoordSpherical,
    MAGtype_CoordGeodetic CoordGeodetic,
    MAGtype_MagneticResults MagneticResultsSph,
    MAGtype_MagneticResults * MagneticResultsGeo);

static void MAG_SphericalToCartesian(MAGtype_CoordSpherical CoordSpherical,
    double *x, double *y, double *z);

/*Spherical Harmonics*/

static int MAG_AssociatedLegendreFunction(MAGtype_CoordSpherical CoordSpherical,
    int nMax, MAGtype_LegendreFunction * LegendreFunction);

static int MAG_ComputeSphericalHarmonicVariables(MAGtype_Ellipsoid Ellip,
    MAGtype_CoordSpherical CoordSpherical,
    int nMax, MAGtype_SphericalHarmonicVariables * SphVariables);

static int MAG_PcupHigh(double *Pcup, double *dPcup, double x, int nMax);

static int MAG_PcupLow(double *Pcup, double *dPcup, double x, int nMax);

static int MAG_SecVarSummation(MAGtype_LegendreFunction * LegendreFunction,
    MAGtype_MagneticModel * MagneticModel,
    MAGtype_SphericalHarmonicVariables SphVariables,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_MagneticResults * MagneticResults);

static int MAG_SecVarSummationSpecial(MAGtype_MagneticModel * MagneticModel,
    MAGtype_SphericalHarmonicVariables SphVariables,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_MagneticResults * MagneticResults);

static int MAG_Summation(MAGtype_LegendreFunction *LegendreFunction,
    MAGtype_MagneticModel * MagneticModel,
    MAGtype_SphericalHarmonicVariables SphVariables,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_MagneticResults * MagneticResults);

static int MAG_SummationSpecial(MAGtype_MagneticModel *MagneticModel,
    MAGtype_SphericalHarmonicVariables SphVariables,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_MagneticResults * MagneticResults);

/******************************************************************************
 ************************************Wrapper***********************************
 * This grouping consists of functions call groups of other functions to do a
 * complete calculation of some sort.  For example, the MAG_Geomag function
 * does everything necessary to compute the geomagnetic elements from a given
 * geodetic point in space and magnetic model adjusted for the appropriate
 * date. These functions are the external functions necessary to create a
 * program that uses or calculates the magnetic field.
 ******************************************************************************
 ******************************************************************************/

int
MAG_Geomag(MAGtype_Ellipsoid Ellip, MAGtype_CoordSpherical CoordSpherical,
    MAGtype_CoordGeodetic CoordGeodetic,
    MAGtype_MagneticModel * TimedMagneticModel,
    MAGtype_GeoMagneticElements * GeoMagneticElements)
/*
The main subroutine that calls a sequence of WMM sub-functions to calculate the magnetic field elements for a single point.
The function expects the model coefficients and point coordinates as input and returns the magnetic field elements and
their rate of change. Though, this subroutine can be called successively to calculate a time series, profile or grid
of magnetic field, these are better achieved by the subroutine MAG_Grid.

INPUT: Ellip
              CoordSpherical
              CoordGeodetic
              TimedMagneticModel

OUTPUT : GeoMagneticElements

CALLS:  	MAG_AllocateLegendreFunctionMemory(NumTerms);  ( For storing the ALF functions )
                     MAG_ComputeSphericalHarmonicVariables( Ellip, CoordSpherical, TimedMagneticModel->nMax, &SphVariables); (Compute Spherical Harmonic variables  )
                     MAG_AssociatedLegendreFunction(CoordSpherical, TimedMagneticModel->nMax, LegendreFunction);  	Compute ALF
                     MAG_Summation(LegendreFunction, TimedMagneticModel, SphVariables, CoordSpherical, &MagneticResultsSph);  Accumulate the spherical harmonic coefficients
                     MAG_SecVarSummation(LegendreFunction, TimedMagneticModel, SphVariables, CoordSpherical, &MagneticResultsSphVar); Sum the Secular Variation Coefficients
                     MAG_RotateMagneticVector(CoordSpherical, CoordGeodetic, MagneticResultsSph, &MagneticResultsGeo); Map the computed Magnetic fields to Geodetic coordinates
                     MAG_CalculateGeoMagneticElements(&MagneticResultsGeo, GeoMagneticElements);   Calculate the Geomagnetic elements
                     MAG_CalculateSecularVariationElements(MagneticResultsGeoVar, GeoMagneticElements); Calculate the secular variation of each of the Geomagnetic elements

 */
{
	MAGtype_LegendreFunction *LegendreFunction;
	MAGtype_SphericalHarmonicVariables *SphVariables;
	int NumTerms;
	MAGtype_MagneticResults MagneticResultsSph, MagneticResultsGeo,
	    MagneticResultsSphVar, MagneticResultsGeoVar;

	NumTerms =
	    ((TimedMagneticModel->nMax + 1) * (TimedMagneticModel->nMax +
		2) / 2);
	LegendreFunction = MAG_AllocateLegendreFunctionMemory(NumTerms);	/* For storing the ALF functions */
	SphVariables = MAG_AllocateSphVarMemory(TimedMagneticModel->nMax);
	MAG_ComputeSphericalHarmonicVariables(Ellip, CoordSpherical, TimedMagneticModel->nMax, SphVariables);	/* Compute Spherical Harmonic variables  */
	MAG_AssociatedLegendreFunction(CoordSpherical, TimedMagneticModel->nMax, LegendreFunction);	/* Compute ALF  */
	MAG_Summation(LegendreFunction, TimedMagneticModel, *SphVariables, CoordSpherical, &MagneticResultsSph);	/* Accumulate the spherical harmonic coefficients */
	MAG_SecVarSummation(LegendreFunction, TimedMagneticModel, *SphVariables, CoordSpherical, &MagneticResultsSphVar);	/*Sum the Secular Variation Coefficients  */
	MAG_RotateMagneticVector(CoordSpherical, CoordGeodetic, MagneticResultsSph, &MagneticResultsGeo);	/* Map the computed Magnetic fields to Geodeitic coordinates  */
	MAG_RotateMagneticVector(CoordSpherical, CoordGeodetic, MagneticResultsSphVar, &MagneticResultsGeoVar);	/* Map the secular variation field components to Geodetic coordinates */
	MAG_CalculateGeoMagneticElements(&MagneticResultsGeo, GeoMagneticElements);	/* Calculate the Geomagnetic elements, Equation 19 , WMM Technical report */
	MAG_CalculateSecularVariationElements(MagneticResultsGeoVar, GeoMagneticElements);	/*Calculate the secular variation of each of the Geomagnetic elements */

	MAG_FreeLegendreMemory(LegendreFunction);
	MAG_FreeSphVarMemory(SphVariables);

	return (TRUE);
}				/*MAG_Geomag */

int
MAG_robustReadMagModels(const char *filename,
    MAGtype_MagneticModel **magneticmodel)
{
	char line[MAXLINELENGTH];
	int n, nMax = 0, num_terms, a;
	FILE *MODELFILE;
	MODELFILE = fopen(filename, "r");
	if (MODELFILE == 0) {
		return (0);
	}
	fgets(line, MAXLINELENGTH, MODELFILE);
	if (line[0] == '%')
		MAG_readMagneticModel_SHDF(filename, magneticmodel);
	else {
		do {
			if (NULL == fgets(line, MAXLINELENGTH, MODELFILE))
				break;
			a = sscanf(line, "%d", &n);
			if (n > nMax && (n < 99999 && a == 1 && n > 0))
				nMax = n;
		} while (n < 99999 && a == 1);
		num_terms = CALCULATE_NUMTERMS(nMax);
		(*magneticmodel) = MAG_AllocateModelMemory(num_terms);
		(*magneticmodel)->nMax = nMax;
		(*magneticmodel)->nMaxSecVar = nMax;
		MAG_readMagneticModel(filename, *magneticmodel);
		(*magneticmodel)->CoefficientFileEndDate =
		    (*magneticmodel)->epoch + 5;

	}
	fclose(MODELFILE);
	return (1);
}				/*MAG_robustReadMagModels */

/*End of Wrapper Functions*/

/******************************************************************************
 ********************************User Interface********************************
 * This grouping consists of functions which interact with the directly with
 * the user and are generally specific to the XXX_point.c, XXX_grid.c, and
 * XXX_file.c programs. They deal with input from and output to the user.
 ******************************************************************************/

void
MAG_Error(int control)

/*This prints WMM errors.
INPUT     control     Error look up number
OUTPUT	  none
CALLS : none

 */
{
	switch (control) {
	case 1:
		printf("\nError allocating in MAG_LegendreFunctionMemory.\n");
		break;
	case 2:
		printf("\nError allocating in MAG_AllocateModelMemory.\n");
		break;
	case 3:
		printf("\nError allocating in MAG_InitializeGeoid\n");
		break;
	case 4:
		printf("\nError in setting default values.\n");
		break;
	case 5:
		printf("\nError initializing Geoid.\n");
		break;
	case 6:
		printf("\nError opening WMM.COF\n.");
		break;
	case 7:
		printf("\nError opening WMMSV.COF\n.");
		break;
	case 8:
		printf("\nError reading Magnetic Model.\n");
		break;
	case 9:
		printf("\nError printing Command Prompt introduction.\n");
		break;
	case 10:
		printf
		    ("\nError converting from geodetic co-ordinates to spherical co-ordinates.\n");
		break;
	case 11:
		printf("\nError in time modifying the Magnetic model\n");
		break;
	case 12:
		printf("\nError in Geomagnetic\n");
		break;
	case 13:
		printf("\nError printing user data\n");
		break;
	case 14:
		printf("\nError allocating in MAG_SummationSpecial\n");
		break;
	case 15:
		printf("\nError allocating in MAG_SecVarSummationSpecial\n");
		break;
	case 16:
		printf("\nError in opening EGM9615.BIN file\n");
		break;
	case 17:
		printf
		    ("\nError: Latitude OR Longitude out of range in MAG_GetGeoidHeight\n");
		break;
	case 18:
		printf("\nError allocating in MAG_PcupHigh\n");
		break;
	case 19:
		printf("\nError allocating in MAG_PcupLow\n");
		break;
	case 20:
		printf("\nError opening coefficient file\n");
		break;
	case 21:
		printf("\nError: UnitDepth too large\n");
		break;
	case 22:
		printf
		    ("\nYour system needs Big endian version of EGM9615.BIN.  \n");
		printf
		    ("Please download this file from http://www.ngdc.noaa.gov/geomag/WMM/DoDWMM.shtml.  \n");
		printf
		    ("Replace the existing EGM9615.BIN file with the downloaded one\n");
		break;
	}
}				/*MAG_Error */

/*End of User Interface functions*/

/******************************************************************************
 ********************************Memory and File Processing********************
 * This grouping consists of functions that read coefficient files into the
 * memory, allocate memory, free memory or print models into coefficient files.
 ******************************************************************************/

MAGtype_LegendreFunction *
MAG_AllocateLegendreFunctionMemory(int NumTerms)

/* Allocate memory for Associated Legendre Function data types.
   Should be called before computing Associated Legendre Functions.

 INPUT: NumTerms : int : Total number of spherical harmonic coefficients in the model

 OUTPUT:    Pointer to data structure MAGtype_LegendreFunction with the following elements
                        double *Pcup;  (  pointer to store Legendre Function  )
                        double *dPcup; ( pointer to store  Derivative of Legendre function )

                        FALSE: Failed to allocate memory

CALLS : none

 */
{
	MAGtype_LegendreFunction *LegendreFunction;

	LegendreFunction =
	    (MAGtype_LegendreFunction *) calloc(1,
	    sizeof (MAGtype_LegendreFunction));

	if (!LegendreFunction) {
		MAG_Error(1);
		return (NULL);
	}
	LegendreFunction->Pcup =
	    (double *)malloc((NumTerms + 1) * sizeof (double));
	if (LegendreFunction->Pcup == 0) {
		MAG_Error(1);
		return (NULL);
	}
	LegendreFunction->dPcup =
	    (double *)malloc((NumTerms + 1) * sizeof (double));
	if (LegendreFunction->dPcup == 0) {
		MAG_Error(1);
		return (NULL);
	}
	return (LegendreFunction);
}				/*MAGtype_LegendreFunction */

MAGtype_MagneticModel *
MAG_AllocateModelMemory(int NumTerms)

/* Allocate memory for WMM Coefficients
 * Should be called before reading the model file *

  INPUT: NumTerms : int : Total number of spherical harmonic coefficients in the model

 OUTPUT:    Pointer to data structure MAGtype_MagneticModel with the following elements
                        double EditionDate;
                        double epoch;       Base time of Geomagnetic model epoch (yrs)
                        char  ModelName[20];
                        double *Main_Field_Coeff_G;          C - Gauss coefficients of main geomagnetic model (nT)
                        double *Main_Field_Coeff_H;          C - Gauss coefficients of main geomagnetic model (nT)
                        double *Secular_Var_Coeff_G;  CD - Gauss coefficients of secular geomagnetic model (nT/yr)
                        double *Secular_Var_Coeff_H;  CD - Gauss coefficients of secular geomagnetic model (nT/yr)
                        int nMax;  Maximum degree of spherical harmonic model
                        int nMaxSecVar; Maxumum degree of spherical harmonic secular model
                        int SecularVariationUsed; Whether or not the magnetic secular variation vector will be needed by program

                        FALSE: Failed to allocate memory
CALLS : none
 */
{
	MAGtype_MagneticModel *MagneticModel;
	int i;

	MagneticModel =
	    (MAGtype_MagneticModel *) calloc(1, sizeof (MAGtype_MagneticModel));

	if (MagneticModel == NULL) {
		MAG_Error(2);
		return (NULL);
	}

	MagneticModel->Main_Field_Coeff_G =
	    (double *)malloc((NumTerms + 1) * sizeof (double));

	if (MagneticModel->Main_Field_Coeff_G == NULL) {
		MAG_Error(2);
		return (NULL);
	}

	MagneticModel->Main_Field_Coeff_H =
	    (double *)malloc((NumTerms + 1) * sizeof (double));

	if (MagneticModel->Main_Field_Coeff_H == NULL) {
		MAG_Error(2);
		return (NULL);
	}
	MagneticModel->Secular_Var_Coeff_G =
	    (double *)malloc((NumTerms + 1) * sizeof (double));
	if (MagneticModel->Secular_Var_Coeff_G == NULL) {
		MAG_Error(2);
		return (NULL);
	}
	MagneticModel->Secular_Var_Coeff_H =
	    (double *)malloc((NumTerms + 1) * sizeof (double));
	if (MagneticModel->Secular_Var_Coeff_H == NULL) {
		MAG_Error(2);
		return (NULL);
	}
	MagneticModel->CoefficientFileEndDate = 0;
	MagneticModel->EditionDate = 0;
	strcpy(MagneticModel->ModelName, "");
	MagneticModel->SecularVariationUsed = 0;
	MagneticModel->epoch = 0;
	MagneticModel->nMax = 0;
	MagneticModel->nMaxSecVar = 0;

	for (i = 0; i < NumTerms; i++) {
		MagneticModel->Main_Field_Coeff_G[i] = 0;
		MagneticModel->Main_Field_Coeff_H[i] = 0;
		MagneticModel->Secular_Var_Coeff_G[i] = 0;
		MagneticModel->Secular_Var_Coeff_H[i] = 0;
	}

	return (MagneticModel);

}				/*MAG_AllocateModelMemory */

MAGtype_SphericalHarmonicVariables *
MAG_AllocateSphVarMemory(int nMax)
{
	MAGtype_SphericalHarmonicVariables *SphVariables;
	SphVariables =
	    (MAGtype_SphericalHarmonicVariables *) calloc(1,
	    sizeof (MAGtype_SphericalHarmonicVariables));
	SphVariables->RelativeRadiusPower =
	    (double *)malloc((nMax + 1) * sizeof (double));
	SphVariables->cos_mlambda =
	    (double *)malloc((nMax + 1) * sizeof (double));
	SphVariables->sin_mlambda =
	    (double *)malloc((nMax + 1) * sizeof (double));
	return (SphVariables);
}				/*MAG_AllocateSphVarMemory */

void
MAG_AssignHeaderValues(MAGtype_MagneticModel * model,
    char values[][MAXLINELENGTH])
{
	/*    MAGtype_Date releasedate; */
	strcpy(model->ModelName, values[MODELNAME]);
	/*      releasedate.Year = 0;
	   releasedate.Day = 0;
	   releasedate.Month = 0;
	   releasedate.DecimalYear = 0;
	   sscanf(values[RELEASEDATE],"%d-%d-%d",&releasedate.Year,&releasedate.Month,&releasedate.Day);
	   if(MAG_DateToYear (&releasedate, NULL))
	   model->EditionDate = releasedate.DecimalYear; */
	model->epoch = atof(values[MODELSTARTYEAR]);
	model->nMax = atoi(values[INTSTATICDEG]);
	model->nMaxSecVar = atoi(values[INTSECVARDEG]);
	model->CoefficientFileEndDate = atof(values[MODELENDYEAR]);
	if (model->nMaxSecVar > 0)
		model->SecularVariationUsed = 1;
	else
		model->SecularVariationUsed = 0;
}

int
MAG_FreeMagneticModelMemory(MAGtype_MagneticModel * MagneticModel)

/* Free the magnetic model memory used by WMM functions.
INPUT :  MagneticModel	pointer to data structure with the following elements

                        double EditionDate;
                        double epoch;       Base time of Geomagnetic model epoch (yrs)
                        char  ModelName[20];
                        double *Main_Field_Coeff_G;          C - Gauss coefficients of main geomagnetic model (nT)
                        double *Main_Field_Coeff_H;          C - Gauss coefficients of main geomagnetic model (nT)
                        double *Secular_Var_Coeff_G;  CD - Gauss coefficients of secular geomagnetic model (nT/yr)
                        double *Secular_Var_Coeff_H;  CD - Gauss coefficients of secular geomagnetic model (nT/yr)
                        int nMax;  Maximum degree of spherical harmonic model
                        int nMaxSecVar; Maxumum degree of spherical harmonic secular model
                        int SecularVariationUsed; Whether or not the magnetic secular variation vector will be needed by program

OUTPUT  none
CALLS : none

 */
{
	if (MagneticModel->Main_Field_Coeff_G) {
		free(MagneticModel->Main_Field_Coeff_G);
		MagneticModel->Main_Field_Coeff_G = NULL;
	}
	if (MagneticModel->Main_Field_Coeff_H) {
		free(MagneticModel->Main_Field_Coeff_H);
		MagneticModel->Main_Field_Coeff_H = NULL;
	}
	if (MagneticModel->Secular_Var_Coeff_G) {
		free(MagneticModel->Secular_Var_Coeff_G);
		MagneticModel->Secular_Var_Coeff_G = NULL;
	}
	if (MagneticModel->Secular_Var_Coeff_H) {
		free(MagneticModel->Secular_Var_Coeff_H);
		MagneticModel->Secular_Var_Coeff_H = NULL;
	}
	if (MagneticModel) {
		free(MagneticModel);
		MagneticModel = NULL;
	}

	return (TRUE);
}				/*MAG_FreeMagneticModelMemory */

int
MAG_FreeLegendreMemory(MAGtype_LegendreFunction * LegendreFunction)

/* Free the Legendre Coefficients memory used by the WMM functions.
INPUT : LegendreFunction Pointer to data structure with the following elements
                                                double *Pcup;  (  pointer to store Legendre Function  )
                                                double *dPcup; ( pointer to store  Derivative of Lagendre function )

OUTPUT: none
CALLS : none

 */
{
	if (LegendreFunction->Pcup) {
		free(LegendreFunction->Pcup);
		LegendreFunction->Pcup = NULL;
	}
	if (LegendreFunction->dPcup) {
		free(LegendreFunction->dPcup);
		LegendreFunction->dPcup = NULL;
	}
	if (LegendreFunction) {
		free(LegendreFunction);
		LegendreFunction = NULL;
	}

	return (TRUE);
}				/*MAG_FreeLegendreMemory */

int
MAG_FreeSphVarMemory(MAGtype_SphericalHarmonicVariables * SphVar)

/* Free the Spherical Harmonic Variable memory used by the WMM functions.
INPUT : LegendreFunction Pointer to data structure with the following elements
                                                double *RelativeRadiusPower
                                                double *cos_mlambda
                                                double *sin_mlambda
 OUTPUT: none
 CALLS : none
 */
{
	if (SphVar->RelativeRadiusPower) {
		free(SphVar->RelativeRadiusPower);
		SphVar->RelativeRadiusPower = NULL;
	}
	if (SphVar->cos_mlambda) {
		free(SphVar->cos_mlambda);
		SphVar->cos_mlambda = NULL;
	}
	if (SphVar->sin_mlambda) {
		free(SphVar->sin_mlambda);
		SphVar->sin_mlambda = NULL;
	}
	if (SphVar) {
		free(SphVar);
		SphVar = NULL;
	}

	return (TRUE);
}				/*MAG_FreeSphVarMemory */

int
MAG_readMagneticModel(const char *filename, MAGtype_MagneticModel * MagneticModel)
{

	/* READ WORLD Magnetic MODEL SPHERICAL HARMONIC COEFFICIENTS (WMM.cof)
	   INPUT :  filename
	   MagneticModel : Pointer to the data structure with the following fields required as inputs
	   nMax :   Number of static coefficients
	   UPDATES : MagneticModel : Pointer to the data structure with the following fields populated
	   char  *ModelName;
	   double epoch;       Base time of Geomagnetic model epoch (yrs)
	   double *Main_Field_Coeff_G;          C - Gauss coefficients of main geomagnetic model (nT)
	   double *Main_Field_Coeff_H;          C - Gauss coefficients of main geomagnetic model (nT)
	   double *Secular_Var_Coeff_G;  CD - Gauss coefficients of secular geomagnetic model (nT/yr)
	   double *Secular_Var_Coeff_H;  CD - Gauss coefficients of secular geomagnetic model (nT/yr)
	   CALLS : none

	 */

	FILE *MAG_COF_File;
	char c_str[81], c_new[5];	/*these strings are used to read a line from coefficient file */
	int i, icomp, m, n, EOF_Flag = 0, index;
	double epoch, gnm, hnm, dgnm, dhnm;
	MAG_COF_File = fopen(filename, "r");

	if (MAG_COF_File == NULL) {
		MAG_Error(20);
		return (FALSE);
		/* should we have a standard error printing routine ? */
	}
	MagneticModel->Main_Field_Coeff_H[0] = 0.0;
	MagneticModel->Main_Field_Coeff_G[0] = 0.0;
	MagneticModel->Secular_Var_Coeff_H[0] = 0.0;
	MagneticModel->Secular_Var_Coeff_G[0] = 0.0;
	fgets(c_str, 80, MAG_COF_File);
	sscanf(c_str, "%lf%s", &epoch, MagneticModel->ModelName);
	MagneticModel->epoch = epoch;
	while (EOF_Flag == 0) {
		fgets(c_str, 80, MAG_COF_File);
		/* CHECK FOR LAST LINE IN FILE */
		for (i = 0; i < 4 && (c_str[i] != '\0'); i++) {
			c_new[i] = c_str[i];
			c_new[i + 1] = '\0';
		}
		icomp = strcmp("9999", c_new);
		if (icomp == 0) {
			EOF_Flag = 1;
			break;
		}
		/* END OF FILE NOT ENCOUNTERED, GET VALUES */
		sscanf(c_str, "%d%d%lf%lf%lf%lf", &n, &m, &gnm, &hnm, &dgnm,
		    &dhnm);
		if (m <= n) {
			index = (n * (n + 1) / 2 + m);
			MagneticModel->Main_Field_Coeff_G[index] = gnm;
			MagneticModel->Secular_Var_Coeff_G[index] = dgnm;
			MagneticModel->Main_Field_Coeff_H[index] = hnm;
			MagneticModel->Secular_Var_Coeff_H[index] = dhnm;
		}
	}

	fclose(MAG_COF_File);
	return (TRUE);
}				/*MAG_readMagneticModel */

int
MAG_readMagneticModel_Large(const char *filename, const char *filenameSV,
    MAGtype_MagneticModel * MagneticModel)

/*  To read the high-degree model coefficients (for example, NGDC 720)
   INPUT :  filename   file name for static coefficients
                        filenameSV file name for secular variation coefficients

                        MagneticModel : Pointer to the data structure with the following fields required as inputs
                                nMaxSecVar : Number of secular variation coefficients
                                nMax : 	Number of static coefficients
   UPDATES : MagneticModel : Pointer to the data structure with the following fields populated
                                double epoch;       Base time of Geomagnetic model epoch (yrs)
                                double *Main_Field_Coeff_G;          C - Gauss coefficients of main geomagnetic model (nT)
                                double *Main_Field_Coeff_H;          C - Gauss coefficients of main geomagnetic model (nT)
                                double *Secular_Var_Coeff_G;  CD - Gauss coefficients of secular geomagnetic model (nT/yr)
                                double *Secular_Var_Coeff_H;  CD - Gauss coefficients of secular geomagnetic model (nT/yr)
        CALLS : none

 */
{
	FILE *MAG_COF_File;
	FILE *MAG_COFSV_File;
	char c_str[81], c_str2[81];	/* these strings are used to read a line from coefficient file */
	int i, m, n, index, a, b;
	double epoch, gnm, hnm, dgnm, dhnm;
	MAG_COF_File = fopen(filename, "r");
	MAG_COFSV_File = fopen(filenameSV, "r");
	if (MAG_COF_File == NULL || MAG_COFSV_File == NULL) {
		MAG_Error(20);
		return (FALSE);
	}
	MagneticModel->Main_Field_Coeff_H[0] = 0.0;
	MagneticModel->Main_Field_Coeff_G[0] = 0.0;
	MagneticModel->Secular_Var_Coeff_H[0] = 0.0;
	MagneticModel->Secular_Var_Coeff_G[0] = 0.0;
	fgets(c_str, 80, MAG_COF_File);
	sscanf(c_str, "%lf%s", &epoch, MagneticModel->ModelName);
	MagneticModel->epoch = epoch;
	a = CALCULATE_NUMTERMS(MagneticModel->nMaxSecVar);
	b = CALCULATE_NUMTERMS(MagneticModel->nMax);
	for (i = 0; i < a; i++) {
		fgets(c_str, 80, MAG_COF_File);
		sscanf(c_str, "%d%d%lf%lf", &n, &m, &gnm, &hnm);
		fgets(c_str2, 80, MAG_COFSV_File);
		sscanf(c_str2, "%d%d%lf%lf", &n, &m, &dgnm, &dhnm);
		if (m <= n) {
			index = (n * (n + 1) / 2 + m);
			MagneticModel->Main_Field_Coeff_G[index] = gnm;
			MagneticModel->Secular_Var_Coeff_G[index] = dgnm;
			MagneticModel->Main_Field_Coeff_H[index] = hnm;
			MagneticModel->Secular_Var_Coeff_H[index] = dhnm;
		}
	}
	for (i = a; i < b; i++) {
		fgets(c_str, 80, MAG_COF_File);
		sscanf(c_str, "%d%d%lf%lf", &n, &m, &gnm, &hnm);
		if (m <= n) {
			index = (n * (n + 1) / 2 + m);
			MagneticModel->Main_Field_Coeff_G[index] = gnm;
			MagneticModel->Main_Field_Coeff_H[index] = hnm;
		}
	}
	if (MAG_COF_File != NULL && MAG_COFSV_File != NULL) {
		fclose(MAG_COF_File);
		fclose(MAG_COFSV_File);
	}

	return (TRUE);
}				/*MAG_readMagneticModel_Large */

static int
MAG_readMagneticModel_SHDF(const char *filename,
    MAGtype_MagneticModel **magneticmodel)
/*
 * MAG_readMagneticModels - Read the Magnetic Models from an SHDF format file
 *
 * Input:
 *  filename - Path to the SHDF format model file to be read
 *
 * Output:
 *  magneticmodel - magnetic model read from the file
 *
 * Return value:
 *  Returns the number of models read from the file.
 *  -2 implies that internal or external static degree was not found in the file, hence memory cannot be allocated
 *  -1 implies some error during file processing (I/O)
 *  0 implies no models were read from the file
 *  if ReturnValue > array_size then there were too many models in model file but only <array_size> number were read .
 *  if ReturnValue <= array_size then the function execution was successful.
 */
{
	char paramkeys[NOOFPARAMS][MAXLINELENGTH] = {
		"SHDF ",
		"ModelName: ",
		"Publisher: ",
		"ReleaseDate: ",
		"DataCutOff: ",
		"ModelStartYear: ",
		"ModelEndYear: ",
		"Epoch: ",
		"IntStaticDeg: ",
		"IntSecVarDeg: ",
		"ExtStaticDeg: ",
		"ExtSecVarDeg: ",
		"GeoMagRefRad: ",
		"Normalization: ",
		"SpatBasFunc: "
	};

	char paramvalues[NOOFPARAMS][MAXLINELENGTH];
	char *line = (char *)malloc(MAXLINELENGTH);
	char *ptrreset;
	char paramvalue[MAXLINELENGTH];
	int paramvaluelength = 0;
	int paramkeylength = 0;
	int i = 0, j = 0;
	int newrecord = 1;
	int header_index = -1;
	int numterms;
	int tempint;
	int allocationflag = 0;
	char coefftype;		/* Internal or External (I/E) */

	/* For reading coefficients */
	int n, m;
	double gnm, hnm, dgnm, dhnm;
	int index;

	FILE *stream;
	ptrreset = line;
	stream = fopen(filename, READONLYMODE);
	if (stream == NULL) {
		perror("File open error");
		return (header_index);
	}

	/* Read records from the model file and store header information. */
	while (fgets(line, MAXLINELENGTH, stream) != NULL) {
		j++;
		if (strlen(MAG_Trim(line)) == 0)
			continue;
		if (*line == '%') {
			line++;
			if (newrecord) {
				if (header_index > -1) {
					MAG_AssignHeaderValues(*magneticmodel,
					    paramvalues);
				}
				header_index++;
				if (header_index >= 1) {
					fprintf(stderr,
					    "Header limit exceeded - too many models in model file. (%d)\n",
					    header_index);
					return (-1);
				}
				newrecord = 0;
				allocationflag = 0;
			}
			for (i = 0; i < NOOFPARAMS; i++) {

				paramkeylength = strlen(paramkeys[i]);
				if (!strncmp(line, paramkeys[i],
					paramkeylength)) {
					paramvaluelength =
					    strlen(line) - paramkeylength;
					strncpy(paramvalue,
					    line + paramkeylength,
					    paramvaluelength);
					paramvalue[paramvaluelength] = '\0';
					strcpy(paramvalues[i], paramvalue);
					if (!strcmp(paramkeys[i],
						paramkeys[INTSTATICDEG]) ||
					    !strcmp(paramkeys[i],
						paramkeys[EXTSTATICDEG])) {
						tempint = atoi(paramvalues[i]);
						if (tempint > 0 &&
						    allocationflag == 0) {
							numterms =
							    CALCULATE_NUMTERMS
							    (tempint);
							(*magneticmodel) =
							    MAG_AllocateModelMemory
							    (numterms);
							allocationflag = 1;
						}
					}
					break;
				}
			}
			line--;
		} else if (*line == '#') {
			/* process comments */

		} else if (sscanf(line, "%c,%d,%d", &coefftype, &n, &m) == 3) {
			if (m == 0) {
				sscanf(line, "%c,%d,%d,%lf,,%lf,", &coefftype,
				    &n, &m, &gnm, &dgnm);
				hnm = 0;
				dhnm = 0;
			} else
				sscanf(line, "%c,%d,%d,%lf,%lf,%lf,%lf",
				    &coefftype, &n, &m, &gnm, &hnm, &dgnm,
				    &dhnm);
			newrecord = 1;
			if (!allocationflag) {
				fprintf(stderr,
				    "Degree not found in model. Memory cannot be allocated.\n");
				return (_DEGREE_NOT_FOUND);
			}
			if (m <= n) {
				index = (n * (n + 1) / 2 + m);
				(*magneticmodel)->Main_Field_Coeff_G[index] =
				    gnm;
				(*magneticmodel)->Secular_Var_Coeff_G[index] =
				    dgnm;
				(*magneticmodel)->Main_Field_Coeff_H[index] =
				    hnm;
				(*magneticmodel)->Secular_Var_Coeff_H[index] =
				    dhnm;
			}
		}
	}
	if (header_index > -1)
		MAG_AssignHeaderValues(*magneticmodel, paramvalues);
	fclose(stream);

	free(ptrreset);
	line = NULL;
	ptrreset = NULL;
	return (header_index + 1);
}				/*MAG_readMagneticModel_SHDF */

static char *
MAG_Trim(char *str)
{
	char *end;

	while (isspace(*str))
		str++;

	if (*str == 0)
		return (str);

	end = str + strlen(str) - 1;
	while (end > str && isspace(*end))
		end--;

	*(end + 1) = 0;

	return (str);
}

/*End of Memory and File Processing functions*/

/******************************************************************************
 *************Conversions, Transformations, and other Calculations**************
 * This grouping consists of functions that perform unit conversions, coordinate
 * transformations and other simple or straightforward calculations that are
 * usually easily replicable with a typical scientific calculator.
 ******************************************************************************/

int
MAG_CalculateGeoMagneticElements(MAGtype_MagneticResults * MagneticResultsGeo,
    MAGtype_GeoMagneticElements * GeoMagneticElements)

/* Calculate all the Geomagnetic elements from X,Y and Z components
INPUT     MagneticResultsGeo   Pointer to data structure with the following elements
                        double Bx;    ( North )
                        double By;	  ( East )
                        double Bz;    ( Down )
OUTPUT    GeoMagneticElements    Pointer to data structure with the following elements
                        double Decl; (Angle between the magnetic field vector and true north, positive east)
                        double Incl; Angle between the magnetic field vector and the horizontal plane, positive down
                        double F; Magnetic Field Strength
                        double H; Horizontal Magnetic Field Strength
                        double X; Northern component of the magnetic field vector
                        double Y; Eastern component of the magnetic field vector
                        double Z; Downward component of the magnetic field vector
CALLS : none
 */
{
	GeoMagneticElements->X = MagneticResultsGeo->Bx;
	GeoMagneticElements->Y = MagneticResultsGeo->By;
	GeoMagneticElements->Z = MagneticResultsGeo->Bz;

	GeoMagneticElements->H =
	    sqrt(MagneticResultsGeo->Bx * MagneticResultsGeo->Bx +
	    MagneticResultsGeo->By * MagneticResultsGeo->By);
	GeoMagneticElements->F =
	    sqrt(GeoMagneticElements->H * GeoMagneticElements->H +
	    MagneticResultsGeo->Bz * MagneticResultsGeo->Bz);
	GeoMagneticElements->Decl =
	    RAD2DEG(atan2(GeoMagneticElements->Y, GeoMagneticElements->X));
	GeoMagneticElements->Incl =
	    RAD2DEG(atan2(GeoMagneticElements->Z, GeoMagneticElements->H));

	return (TRUE);
}				/*MAG_CalculateGeoMagneticElements */

int
MAG_CalculateSecularVariationElements(MAGtype_MagneticResults MagneticVariation,
    MAGtype_GeoMagneticElements * MagneticElements)
/*This takes the Magnetic Variation in x, y, and z and uses it to calculate the secular variation of each of the Geomagnetic elements.
        INPUT     MagneticVariation   Data structure with the following elements
                                double Bx;    ( North )
                                double By;	  ( East )
                                double Bz;    ( Down )
        OUTPUT   MagneticElements   Pointer to the data  structure with the following elements updated
                        double Decldot; Yearly Rate of change in declination
                        double Incldot; Yearly Rate of change in inclination
                        double Fdot; Yearly rate of change in Magnetic field strength
                        double Hdot; Yearly rate of change in horizontal field strength
                        double Xdot; Yearly rate of change in the northern component
                        double Ydot; Yearly rate of change in the eastern component
                        double Zdot; Yearly rate of change in the downward component
                        double GVdot;Yearly rate of chnage in grid variation
        CALLS : none

 */
{
	MagneticElements->Xdot = MagneticVariation.Bx;
	MagneticElements->Ydot = MagneticVariation.By;
	MagneticElements->Zdot = MagneticVariation.Bz;
	MagneticElements->Hdot = (MagneticElements->X * MagneticElements->Xdot + MagneticElements->Y * MagneticElements->Ydot) / MagneticElements->H;	/* See equation 19 in the WMM technical report */
	MagneticElements->Fdot =
	    (MagneticElements->X * MagneticElements->Xdot +
	    MagneticElements->Y * MagneticElements->Ydot +
	    MagneticElements->Z * MagneticElements->Zdot) / MagneticElements->F;
	MagneticElements->Decldot =
	    180.0 / M_PI * (MagneticElements->X * MagneticElements->Ydot -
	    MagneticElements->Y * MagneticElements->Xdot) /
	    (MagneticElements->H * MagneticElements->H);
	MagneticElements->Incldot =
	    180.0 / M_PI * (MagneticElements->H * MagneticElements->Zdot -
	    MagneticElements->Z * MagneticElements->Hdot) /
	    (MagneticElements->F * MagneticElements->F);
	MagneticElements->GVdot = MagneticElements->Decldot;
	return (TRUE);
}				/*MAG_CalculateSecularVariationElements */

void
MAG_CartesianToGeodetic(MAGtype_Ellipsoid Ellip, double x, double y, double z,
    MAGtype_CoordGeodetic * CoordGeodetic)
{
	/*This converts the Cartesian x, y, and z coordinates to Geodetic Coordinates
	   x is defined as the direction pointing out of the core toward the point defined
	   * by 0 degrees latitude and longitude.
	   y is defined as the direction from the core toward 90 degrees east longitude along
	   * the equator
	   z is defined as the direction from the core out the geographic north pole */

	double modified_b, r, e, f, p, q, d, v, g, t, zlong, rlat;

/*
 *   1.0 compute semi-minor axis and set sign to that of z in order
 *       to get sign of Phi correct
 */

	if (z < 0.0)
		modified_b = -Ellip.b;
	else
		modified_b = Ellip.b;

/*
 *   2.0 compute intermediate values for latitude
 */
	r = sqrt(x * x + y * y);
	e = (modified_b * z - (Ellip.a * Ellip.a -
		modified_b * modified_b)) / (Ellip.a * r);
	f = (modified_b * z + (Ellip.a * Ellip.a -
		modified_b * modified_b)) / (Ellip.a * r);
/*
 *   3.0 find solution to:
 *       t^4 + 2*E*t^3 + 2*F*t - 1 = 0
 */
	p = (4.0 / 3.0) * (e * f + 1.0);
	q = 2.0 * (e * e - f * f);
	d = p * p * p + q * q;

	if (d >= 0.0) {
		v = pow((sqrt(d) - q), (1.0 / 3.0))
		    - pow((sqrt(d) + q), (1.0 / 3.0));
	} else {
		v = 2.0 * sqrt(-p)
		    * cos(acos(q / (p * sqrt(-p))) / 3.0);
	}
/*
 *   4.0 improve v
 *       NOTE: not really necessary unless point is near pole
 */
	if (v * v < fabs(p)) {
		v = -(v * v * v + 2.0 * q) / (3.0 * p);
	}
	g = (sqrt(e * e + v) + e) / 2.0;
	t = sqrt(g * g + (f - v * g) / (2.0 * g - e)) - g;

	rlat = atan((Ellip.a * (1.0 - t * t)) / (2.0 * modified_b * t));
	CoordGeodetic->phi = RAD2DEG(rlat);

/*
 *   5.0 compute height above ellipsoid
 */
	CoordGeodetic->HeightAboveEllipsoid =
	    (r - Ellip.a * t) * cos(rlat) + (z - modified_b) * sin(rlat);
/*
 *   6.0 compute longitude east of Greenwich
 */
	zlong = atan2(y, x);
	if (zlong < 0.0)
		zlong = zlong + 2 * M_PI;

	CoordGeodetic->lambda = RAD2DEG(zlong);
	while (CoordGeodetic->lambda > 180) {
		CoordGeodetic->lambda -= 360;
	}

}

int
MAG_GeodeticToSpherical(MAGtype_Ellipsoid Ellip,
    MAGtype_CoordGeodetic CoordGeodetic,
    MAGtype_CoordSpherical * CoordSpherical)

/* Converts Geodetic coordinates to Spherical coordinates

  INPUT   Ellip  data  structure with the following elements
                        double a; semi-major axis of the ellipsoid
                        double b; semi-minor axis of the ellipsoid
                        double fla;  flattening
                        double epssq; first eccentricity squared
                        double eps;  first eccentricity
                        double re; mean radius of  ellipsoid

                CoordGeodetic  Pointer to the  data  structure with the following elements updates
                        double lambda; ( longitude )
                        double phi; ( geodetic latitude )
                        double HeightAboveEllipsoid; ( height above the WGS84 ellipsoid (HaE) )
                        double HeightAboveGeoid; (height above the EGM96 Geoid model )

 OUTPUT		CoordSpherical 	Pointer to the data structure with the following elements
                        double lambda; ( longitude)
                        double phig; ( geocentric latitude )
                        double r;  	  ( distance from the center of the ellipsoid)

CALLS : none

 */
{
	double CosLat, SinLat, rc, xp, zp;	/*all local variables */

	/*
	 ** Convert geodetic coordinates, (defined by the WGS-84
	 ** reference ellipsoid), to Earth Centered Earth Fixed Cartesian
	 ** coordinates, and then to spherical coordinates.
	 */

	CosLat = cos(DEG2RAD(CoordGeodetic.phi));
	SinLat = sin(DEG2RAD(CoordGeodetic.phi));

	/* compute the local radius of curvature on the WGS-84 reference ellipsoid */

	rc = Ellip.a / sqrt(1.0 - Ellip.epssq * SinLat * SinLat);

	/* compute ECEF Cartesian coordinates of specified point (for longitude=0) */

	xp = (rc + CoordGeodetic.HeightAboveEllipsoid) * CosLat;
	zp = (rc * (1.0 - Ellip.epssq) +
	    CoordGeodetic.HeightAboveEllipsoid) * SinLat;

	/* compute spherical radius and angle lambda and phi of specified point */

	CoordSpherical->r = sqrt(xp * xp + zp * zp);
	CoordSpherical->phig = RAD2DEG(asin(zp / CoordSpherical->r));	/* geocentric latitude */
	CoordSpherical->lambda = CoordGeodetic.lambda;	/* longitude */

	return (TRUE);
}				/*MAG_GeodeticToSpherical */

int
MAG_RotateMagneticVector(MAGtype_CoordSpherical CoordSpherical,
    MAGtype_CoordGeodetic CoordGeodetic,
    MAGtype_MagneticResults MagneticResultsSph,
    MAGtype_MagneticResults * MagneticResultsGeo)
/* Rotate the Magnetic Vectors to Geodetic Coordinates
Manoj Nair, June, 2009 Manoj.C.Nair@Noaa.Gov
Equation 16, WMM Technical report

INPUT : CoordSpherical : Data structure MAGtype_CoordSpherical with the following elements
                        double lambda; ( longitude)
                        double phig; ( geocentric latitude )
                        double r;  	  ( distance from the center of the ellipsoid)

                CoordGeodetic : Data structure MAGtype_CoordGeodetic with the following elements
                        double lambda; (longitude)
                        double phi; ( geodetic latitude)
                        double HeightAboveEllipsoid; (height above the ellipsoid (HaE) )
                        double HeightAboveGeoid;(height above the Geoid )

                MagneticResultsSph : Data structure MAGtype_MagneticResults with the following elements
                        double Bx;      North
                        double By;      East
                        double Bz;      Down

OUTPUT: MagneticResultsGeo Pointer to the data structure MAGtype_MagneticResults, with the following elements
                        double Bx;      North
                        double By;      East
                        double Bz;      Down

CALLS : none

 */
{
	double Psi;
	/* Difference between the spherical and Geodetic latitudes */
	Psi = (M_PI / 180) * (CoordSpherical.phig - CoordGeodetic.phi);

	/* Rotate spherical field components to the Geodetic system */
	MagneticResultsGeo->Bz =
	    MagneticResultsSph.Bx * sin(Psi) + MagneticResultsSph.Bz * cos(Psi);
	MagneticResultsGeo->Bx =
	    MagneticResultsSph.Bx * cos(Psi) - MagneticResultsSph.Bz * sin(Psi);
	MagneticResultsGeo->By = MagneticResultsSph.By;
	return (TRUE);
}				/*MAG_RotateMagneticVector */

void
MAG_SphericalToCartesian(MAGtype_CoordSpherical CoordSpherical, double *x,
    double *y, double *z)
{
	double radphi;
	double radlambda;

	radphi = CoordSpherical.phig * (M_PI / 180);
	radlambda = CoordSpherical.lambda * (M_PI / 180);

	*x = CoordSpherical.r * cos(radphi) * cos(radlambda);
	*y = CoordSpherical.r * cos(radphi) * sin(radlambda);
	*z = CoordSpherical.r * sin(radphi);
	return;
}

void
MAG_SphericalToGeodetic(MAGtype_Ellipsoid Ellip,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_CoordGeodetic * CoordGeodetic)
{
	/*This converts spherical coordinates back to geodetic coordinates.  It is not used in the WMM but
	   may be necessary for some applications, such as geomagnetic coordinates */
	double x, y, z;

	MAG_SphericalToCartesian(CoordSpherical, &x, &y, &z);
	MAG_CartesianToGeodetic(Ellip, x, y, z, CoordGeodetic);
}

/******************************************************************************
 ********************************Spherical Harmonics***************************
 * This grouping consists of functions that together take gauss coefficients
 * and return a magnetic vector for an input location in spherical coordinates
 ******************************************************************************/

int
MAG_AssociatedLegendreFunction(MAGtype_CoordSpherical CoordSpherical, int nMax,
    MAGtype_LegendreFunction * LegendreFunction)

/* Computes  all of the Schmidt-semi normalized associated Legendre
functions up to degree nMax. If nMax <= 16, function MAG_PcupLow is used.
Otherwise MAG_PcupHigh is called.
INPUT  CoordSpherical 	A data structure with the following elements
                                                double lambda; ( longitude)
                                                double phig; ( geocentric latitude )
                                                double r;  	  ( distance from the center of the ellipsoid)
                nMax        	integer 	 ( Maxumum degree of spherical harmonic secular model)
                LegendreFunction Pointer to data structure with the following elements
                                                double *Pcup;  (  pointer to store Legendre Function  )
                                                double *dPcup; ( pointer to store  Derivative of Lagendre function )

OUTPUT  LegendreFunction  Calculated Legendre variables in the data structure

 */
{
	double sin_phi;
	int FLAG = 1;

	sin_phi = sin(DEG2RAD(CoordSpherical.phig));	/* sin  (geocentric latitude) */

	if (nMax <= 16 || (1 - fabs(sin_phi)) < 1.0e-10)	/* If nMax is less tha 16 or at the poles */
		FLAG =
		    MAG_PcupLow(LegendreFunction->Pcup, LegendreFunction->dPcup,
		    sin_phi, nMax);
	else
		FLAG =
		    MAG_PcupHigh(LegendreFunction->Pcup,
		    LegendreFunction->dPcup, sin_phi, nMax);
	if (FLAG == 0)		/* Error while computing  Legendre variables */
		return (FALSE);

	return (TRUE);
}				/*MAG_AssociatedLegendreFunction */

int
MAG_ComputeSphericalHarmonicVariables(MAGtype_Ellipsoid Ellip,
    MAGtype_CoordSpherical CoordSpherical, int nMax,
    MAGtype_SphericalHarmonicVariables * SphVariables)

/* Computes Spherical variables
       Variables computed are (a/r)^(n+2), cos_m(lamda) and sin_m(lambda) for spherical harmonic
       summations. (Equations 10-12 in the WMM Technical Report)
       INPUT   Ellip  data  structure with the following elements
                             double a; semi-major axis of the ellipsoid
                             double b; semi-minor axis of the ellipsoid
                             double fla;  flattening
                             double epssq; first eccentricity squared
                             double eps;  first eccentricity
                             double re; mean radius of  ellipsoid
                     CoordSpherical 	A data structure with the following elements
                             double lambda; ( longitude)
                             double phig; ( geocentric latitude )
                             double r;  	  ( distance from the center of the ellipsoid)
                     nMax   integer 	 ( Maxumum degree of spherical harmonic secular model)\

     OUTPUT  SphVariables  Pointer to the   data structure with the following elements
             double RelativeRadiusPower[MAG_MAX_MODEL_DEGREES+1];   [earth_reference_radius_km  sph. radius ]^n
             double cos_mlambda[MAG_MAX_MODEL_DEGREES+1]; cp(m)  - cosine of (mspherical coord. longitude)
             double sin_mlambda[MAG_MAX_MODEL_DEGREES+1];  sp(m)  - sine of (mspherical coord. longitude)
     CALLS : none
 */
{
	double cos_lambda, sin_lambda;
	int m, n;
	cos_lambda = cos(DEG2RAD(CoordSpherical.lambda));
	sin_lambda = sin(DEG2RAD(CoordSpherical.lambda));
	/* for n = 0 ... model_order, compute (Radius of Earth / Spherical radius r)^(n+2)
	   for n  1..nMax-1 (this is much faster than calling pow MAX_N+1 times).      */
	SphVariables->RelativeRadiusPower[0] =
	    (Ellip.re / CoordSpherical.r) * (Ellip.re / CoordSpherical.r);
	for (n = 1; n <= nMax; n++) {
		SphVariables->RelativeRadiusPower[n] =
		    SphVariables->RelativeRadiusPower[n -
		    1] * (Ellip.re / CoordSpherical.r);
	}

	/*
	   Compute cos(m*lambda), sin(m*lambda) for m = 0 ... nMax
	   cos(a + b) = cos(a)*cos(b) - sin(a)*sin(b)
	   sin(a + b) = cos(a)*sin(b) + sin(a)*cos(b)
	 */
	SphVariables->cos_mlambda[0] = 1.0;
	SphVariables->sin_mlambda[0] = 0.0;

	SphVariables->cos_mlambda[1] = cos_lambda;
	SphVariables->sin_mlambda[1] = sin_lambda;
	for (m = 2; m <= nMax; m++) {
		SphVariables->cos_mlambda[m] =
		    SphVariables->cos_mlambda[m - 1] * cos_lambda -
		    SphVariables->sin_mlambda[m - 1] * sin_lambda;
		SphVariables->sin_mlambda[m] =
		    SphVariables->cos_mlambda[m - 1] * sin_lambda +
		    SphVariables->sin_mlambda[m - 1] * cos_lambda;
	}
	return (TRUE);
}				/*MAG_ComputeSphericalHarmonicVariables */

int
MAG_PcupHigh(double *Pcup, double *dPcup, double x, int nMax)

/*	This function evaluates all of the Schmidt-semi normalized associated Legendre
        functions up to degree nMax. The functions are initially scaled by
        10^280 sin^m in order to minimize the effects of underflow at large m
        near the poles (see Holmes and Featherstone 2002, J. Geodesy, 76, 279-299).
        Note that this function performs the same operation as MAG_PcupLow.
        However this function also can be used for high degree (large nMax) models.

        Calling Parameters:
                INPUT
                        nMax:	 Maximum spherical harmonic degree to compute.
                        x:		cos(colatitude) or sin(latitude).

                OUTPUT
                        Pcup:	A vector of all associated Legendgre polynomials evaluated at
                                        x up to nMax. The lenght must by greater or equal to (nMax+1)*(nMax+2)/2.
                  dPcup:   Derivative of Pcup(x) with respect to latitude

                CALLS : none
        Notes:

  Adopted from the FORTRAN code written by Mark Wieczorek September 25, 2005.

  Manoj Nair, Nov, 2009 Manoj.C.Nair@Noaa.Gov

  Change from the previous version
  The prevous version computes the derivatives as
  dP(n,m)(x)/dx, where x = sin(latitude) (or cos(colatitude) ).
  However, the WMM Geomagnetic routines requires dP(n,m)(x)/dlatitude.
  Hence the derivatives are multiplied by sin(latitude).
  Removed the options for CS phase and normalizations.

  Note: In geomagnetism, the derivatives of ALF are usually found with
  respect to the colatitudes. Here the derivatives are found with respect
  to the latitude. The difference is a sign reversal for the derivative of
  the Associated Legendre Functions.

  The derivatives can't be computed for latitude = |90| degrees.
 */
{
	double pm2, pm1, pmm, plm, rescalem, z, scalef;
	double *f1, *f2, *PreSqr;
	int k, kstart, m, n, NumTerms;

	NumTerms = ((nMax + 1) * (nMax + 2) / 2);

	if (fabs(x) == 1.0) {
		printf
		    ("Error in PcupHigh: derivative cannot be calculated at poles\n");
		return (FALSE);
	}

	f1 = (double *)malloc((NumTerms + 1) * sizeof (double));
	if (f1 == NULL) {
		MAG_Error(18);
		return (FALSE);
	}

	PreSqr = (double *)malloc((NumTerms + 1) * sizeof (double));

	if (PreSqr == NULL) {
		MAG_Error(18);
		return (FALSE);
	}

	f2 = (double *)malloc((NumTerms + 1) * sizeof (double));

	if (f2 == NULL) {
		MAG_Error(18);
		return (FALSE);
	}

	scalef = 1.0e-280;

	for (n = 0; n <= 2 * nMax + 1; ++n) {
		PreSqr[n] = sqrt((double)(n));
	}

	k = 2;

	for (n = 2; n <= nMax; n++) {
		k = k + 1;
		f1[k] = (double)(2 * n - 1) / (double)(n);
		f2[k] = (double)(n - 1) / (double)(n);
		for (m = 1; m <= n - 2; m++) {
			k = k + 1;
			f1[k] =
			    (double)(2 * n - 1) / PreSqr[n + m] / PreSqr[n - m];
			f2[k] =
			    PreSqr[n - m - 1] * PreSqr[n + m - 1] / PreSqr[n +
			    m] / PreSqr[n - m];
		}
		k = k + 2;
	}

	/*z = sin (geocentric latitude) */
	z = sqrt((1.0 - x) * (1.0 + x));
	pm2 = 1.0;
	Pcup[0] = 1.0;
	dPcup[0] = 0.0;
	if (nMax == 0)
		return (FALSE);
	pm1 = x;
	Pcup[1] = pm1;
	dPcup[1] = z;
	k = 1;

	for (n = 2; n <= nMax; n++) {
		k = k + n;
		plm = f1[k] * x * pm1 - f2[k] * pm2;
		Pcup[k] = plm;
		dPcup[k] = (double)(n) * (pm1 - x * plm) / z;
		pm2 = pm1;
		pm1 = plm;
	}

	pmm = PreSqr[2] * scalef;
	rescalem = 1.0 / scalef;
	kstart = 0;

	for (m = 1; m <= nMax - 1; ++m) {
		rescalem = rescalem * z;

		/* Calculate Pcup(m,m) */
		kstart = kstart + m + 1;
		pmm = pmm * PreSqr[2 * m + 1] / PreSqr[2 * m];
		Pcup[kstart] = pmm * rescalem / PreSqr[2 * m + 1];
		dPcup[kstart] = -((double)(m) * x * Pcup[kstart] / z);
		pm2 = pmm / PreSqr[2 * m + 1];
		/* Calculate Pcup(m+1,m) */
		k = kstart + m + 1;
		pm1 = x * PreSqr[2 * m + 1] * pm2;
		Pcup[k] = pm1 * rescalem;
		dPcup[k] =
		    ((pm2 * rescalem) * PreSqr[2 * m + 1] - x * (double)(m +
			1) * Pcup[k]) / z;
		/* Calculate Pcup(n,m) */
		for (n = m + 2; n <= nMax; ++n) {
			k = k + n;
			plm = x * f1[k] * pm1 - f2[k] * pm2;
			Pcup[k] = plm * rescalem;
			dPcup[k] =
			    (PreSqr[n + m] * PreSqr[n - m] * (pm1 * rescalem) -
			    (double)(n) * x * Pcup[k]) / z;
			pm2 = pm1;
			pm1 = plm;
		}
	}

	/* Calculate Pcup(nMax,nMax) */
	rescalem = rescalem * z;
	kstart = kstart + m + 1;
	pmm = pmm / PreSqr[2 * nMax];
	Pcup[kstart] = pmm * rescalem;
	dPcup[kstart] = -(double)(nMax) * x * Pcup[kstart] / z;
	free(f1);
	free(PreSqr);
	free(f2);

	return (TRUE);
}				/* MAG_PcupHigh */

int
MAG_PcupLow(double *Pcup, double *dPcup, double x, int nMax)

/*   This function evaluates all of the Schmidt-semi normalized associated Legendre
        functions up to degree nMax.

        Calling Parameters:
                INPUT
                        nMax:	 Maximum spherical harmonic degree to compute.
                        x:		cos(colatitude) or sin(latitude).

                OUTPUT
                        Pcup:	A vector of all associated Legendgre polynomials evaluated at
                                        x up to nMax.
                   dPcup: Derivative of Pcup(x) with respect to latitude

        Notes: Overflow may occur if nMax > 20 , especially for high-latitudes.
        Use MAG_PcupHigh for large nMax.

   Written by Manoj Nair, June, 2009 . Manoj.C.Nair@Noaa.Gov.

  Note: In geomagnetism, the derivatives of ALF are usually found with
  respect to the colatitudes. Here the derivatives are found with respect
  to the latitude. The difference is a sign reversal for the derivative of
  the Associated Legendre Functions.
 */
{
	int n, m, index, index1, index2, NumTerms;
	double k, z, *schmidtQuasiNorm;
	Pcup[0] = 1.0;
	dPcup[0] = 0.0;
	/*sin (geocentric latitude) - sin_phi */
	z = sqrt((1.0 - x) * (1.0 + x));

	NumTerms = ((nMax + 1) * (nMax + 2) / 2);
	schmidtQuasiNorm = (double *)malloc((NumTerms + 1) * sizeof (double));

	if (schmidtQuasiNorm == NULL) {
		MAG_Error(19);
		return (FALSE);
	}

	/*   First, Compute the Gauss-normalized associated Legendre  functions */
	for (n = 1; n <= nMax; n++) {
		for (m = 0; m <= n; m++) {
			index = (n * (n + 1) / 2 + m);
			if (n == m) {
				index1 = (n - 1) * n / 2 + m - 1;
				Pcup[index] = z * Pcup[index1];
				dPcup[index] =
				    z * dPcup[index1] + x * Pcup[index1];
			} else if (n == 1 && m == 0) {
				index1 = (n - 1) * n / 2 + m;
				Pcup[index] = x * Pcup[index1];
				dPcup[index] =
				    x * dPcup[index1] - z * Pcup[index1];
			} else if (n > 1 && n != m) {
				index1 = (n - 2) * (n - 1) / 2 + m;
				index2 = (n - 1) * n / 2 + m;
				if (m > n - 2) {
					Pcup[index] = x * Pcup[index2];
					dPcup[index] =
					    x * dPcup[index2] -
					    z * Pcup[index2];
				} else {
					k = (double)(((n - 1) * (n - 1)) -
					    (m * m)) / (double)((2 * n -
						1) * (2 * n - 3));
					Pcup[index] =
					    x * Pcup[index2] - k * Pcup[index1];
					dPcup[index] =
					    x * dPcup[index2] -
					    z * Pcup[index2] -
					    k * dPcup[index1];
				}
			}
		}
	}
	/* Compute the ration between the the Schmidt quasi-normalized associated Legendre
	 * functions and the Gauss-normalized version. */

	schmidtQuasiNorm[0] = 1.0;
	for (n = 1; n <= nMax; n++) {
		index = (n * (n + 1) / 2);
		index1 = (n - 1) * n / 2;
		/* for m = 0 */
		schmidtQuasiNorm[index] =
		    schmidtQuasiNorm[index1] * (double)(2 * n - 1) / (double)n;

		for (m = 1; m <= n; m++) {
			index = (n * (n + 1) / 2 + m);
			index1 = (n * (n + 1) / 2 + m - 1);
			schmidtQuasiNorm[index] =
			    schmidtQuasiNorm[index1] * sqrt((double)((n - m +
				    1) * (m == 1 ? 2 : 1)) / (double)(n + m));
		}

	}

	/* Converts the  Gauss-normalized associated Legendre
	   functions to the Schmidt quasi-normalized version using pre-computed
	   relation stored in the variable schmidtQuasiNorm */

	for (n = 1; n <= nMax; n++) {
		for (m = 0; m <= n; m++) {
			index = (n * (n + 1) / 2 + m);
			Pcup[index] = Pcup[index] * schmidtQuasiNorm[index];
			dPcup[index] = -dPcup[index] * schmidtQuasiNorm[index];
			/* The sign is changed since the new WMM routines use derivative with respect to latitude
			   insted of co-latitude */
		}
	}

	if (schmidtQuasiNorm)
		free(schmidtQuasiNorm);
	return (TRUE);
}				/*MAG_PcupLow */

int
MAG_SecVarSummation(MAGtype_LegendreFunction * LegendreFunction,
    MAGtype_MagneticModel * MagneticModel,
    MAGtype_SphericalHarmonicVariables SphVariables,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_MagneticResults * MagneticResults)
{
	/*This Function sums the secular variation coefficients to get the secular variation of the Magnetic vector.
	   INPUT :  LegendreFunction
	   MagneticModel
	   SphVariables
	   CoordSpherical
	   OUTPUT : MagneticResults

	   CALLS : MAG_SecVarSummationSpecial

	 */
	int m, n, index;
	double cos_phi;
	MagneticModel->SecularVariationUsed = TRUE;
	MagneticResults->Bz = 0.0;
	MagneticResults->By = 0.0;
	MagneticResults->Bx = 0.0;
	for (n = 1; n <= MagneticModel->nMaxSecVar; n++) {
		for (m = 0; m <= n; m++) {
			index = (n * (n + 1) / 2 + m);

			/*              nMax        (n+2)     n     m            m           m
			   Bz =   -SUM (a/r)   (n+1) SUM  [g cos(m p) + h sin(m p)] P (sin(phi))
			   n=1                    m=0   n            n           n  */
			/*  Derivative with respect to radius. */
			MagneticResults->Bz -=
			    SphVariables.RelativeRadiusPower[n] *
			    (MagneticModel->Secular_Var_Coeff_G[index] *
			    SphVariables.cos_mlambda[m] +
			    MagneticModel->Secular_Var_Coeff_H[index] *
			    SphVariables.sin_mlambda[m])
			    * (double)(n + 1) * LegendreFunction->Pcup[index];

			/*            1 nMax  (n+2)    n     m            m           m
			   By =    SUM (a/r) (m)  SUM  [g cos(m p) + h sin(m p)] dP (sin(phi))
			   n=1             m=0   n            n           n  */
			/* Derivative with respect to longitude, divided by radius. */
			MagneticResults->By +=
			    SphVariables.RelativeRadiusPower[n] *
			    (MagneticModel->Secular_Var_Coeff_G[index] *
			    SphVariables.sin_mlambda[m] -
			    MagneticModel->Secular_Var_Coeff_H[index] *
			    SphVariables.cos_mlambda[m])
			    * (double)(m) * LegendreFunction->Pcup[index];
			/*             nMax  (n+2) n     m            m           m
			   Bx = - SUM (a/r)   SUM  [g cos(m p) + h sin(m p)] dP (sin(phi))
			   n=1         m=0   n            n           n  */
			/* Derivative with respect to latitude, divided by radius. */

			MagneticResults->Bx -=
			    SphVariables.RelativeRadiusPower[n] *
			    (MagneticModel->Secular_Var_Coeff_G[index] *
			    SphVariables.cos_mlambda[m] +
			    MagneticModel->Secular_Var_Coeff_H[index] *
			    SphVariables.sin_mlambda[m])
			    * LegendreFunction->dPcup[index];
		}
	}
	cos_phi = cos(DEG2RAD(CoordSpherical.phig));
	if (fabs(cos_phi) > 1.0e-10) {
		MagneticResults->By = MagneticResults->By / cos_phi;
	} else
		/* Special calculation for component By at Geographic poles */
	{
		MAG_SecVarSummationSpecial(MagneticModel, SphVariables,
		    CoordSpherical, MagneticResults);
	}
	return (TRUE);
}				/*MAG_SecVarSummation */

int
MAG_SecVarSummationSpecial(MAGtype_MagneticModel * MagneticModel,
    MAGtype_SphericalHarmonicVariables SphVariables,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_MagneticResults * MagneticResults)
{
	/*Special calculation for the secular variation summation at the poles.

	   INPUT: MagneticModel
	   SphVariables
	   CoordSpherical
	   OUTPUT: MagneticResults
	   CALLS : none

	 */
	int n, index;
	double k, sin_phi, *PcupS, schmidtQuasiNorm1, schmidtQuasiNorm2,
	    schmidtQuasiNorm3;

	PcupS =
	    (double *)malloc((MagneticModel->nMaxSecVar + 1) * sizeof (double));

	if (PcupS == NULL) {
		MAG_Error(15);
		return (FALSE);
	}

	PcupS[0] = 1;
	schmidtQuasiNorm1 = 1.0;

	MagneticResults->By = 0.0;
	sin_phi = sin(DEG2RAD(CoordSpherical.phig));

	for (n = 1; n <= MagneticModel->nMaxSecVar; n++) {
		index = (n * (n + 1) / 2 + 1);
		schmidtQuasiNorm2 =
		    schmidtQuasiNorm1 * (double)(2 * n - 1) / (double)n;
		schmidtQuasiNorm3 =
		    schmidtQuasiNorm2 * sqrt((double)(n * 2) / (double)(n + 1));
		schmidtQuasiNorm1 = schmidtQuasiNorm2;
		if (n == 1) {
			PcupS[n] = PcupS[n - 1];
		} else {
			k = (double)(((n - 1) * (n - 1)) -
			    1) / (double)((2 * n - 1) * (2 * n - 3));
			PcupS[n] = sin_phi * PcupS[n - 1] - k * PcupS[n - 2];
		}

		/*                1 nMax  (n+2)    n     m            m           m
		   By =    SUM (a/r) (m)  SUM  [g cos(m p) + h sin(m p)] dP (sin(phi))
		   n=1             m=0   n            n           n  */
		/* Derivative with respect to longitude, divided by radius. */

		MagneticResults->By += SphVariables.RelativeRadiusPower[n] *
		    (MagneticModel->Secular_Var_Coeff_G[index] *
		    SphVariables.sin_mlambda[1] -
		    MagneticModel->Secular_Var_Coeff_H[index] *
		    SphVariables.cos_mlambda[1])
		    * PcupS[n] * schmidtQuasiNorm3;
	}

	if (PcupS)
		free(PcupS);
	return (TRUE);
}				/*SecVarSummationSpecial */

int
MAG_Summation(MAGtype_LegendreFunction * LegendreFunction,
    MAGtype_MagneticModel * MagneticModel,
    MAGtype_SphericalHarmonicVariables SphVariables,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_MagneticResults * MagneticResults)
{
	/* Computes Geomagnetic Field Elements X, Y and Z in Spherical coordinate system using
	   spherical harmonic summation.

	   The vector Magnetic field is given by -grad V, where V is Geomagnetic scalar potential
	   The gradient in spherical coordinates is given by:

	   dV ^     1 dV ^        1     dV ^
	   grad V = -- r  +  - -- t  +  -------- -- p
	   dr       r dt       r sin(t) dp

	   INPUT :  LegendreFunction
	   MagneticModel
	   SphVariables
	   CoordSpherical
	   OUTPUT : MagneticResults

	   CALLS : MAG_SummationSpecial

	   Manoj Nair, June, 2009 Manoj.C.Nair@Noaa.Gov
	 */
	int m, n, index;
	double cos_phi;
	MagneticResults->Bz = 0.0;
	MagneticResults->By = 0.0;
	MagneticResults->Bx = 0.0;
	for (n = 1; n <= MagneticModel->nMax; n++) {
		for (m = 0; m <= n; m++) {
			index = (n * (n + 1) / 2 + m);

			/*              nMax        (n+2)     n     m            m           m
			   Bz =   -SUM (a/r)   (n+1) SUM  [g cos(m p) + h sin(m p)] P (sin(phi))
			   n=1                    m=0   n            n           n  */
			/* Equation 12 in the WMM Technical report.  Derivative with respect to radius. */
			MagneticResults->Bz -=
			    SphVariables.RelativeRadiusPower[n] *
			    (MagneticModel->Main_Field_Coeff_G[index] *
			    SphVariables.cos_mlambda[m] +
			    MagneticModel->Main_Field_Coeff_H[index] *
			    SphVariables.sin_mlambda[m])
			    * (double)(n + 1) * LegendreFunction->Pcup[index];

			/*            1 nMax  (n+2)    n     m            m           m
			   By =    SUM (a/r) (m)  SUM  [g cos(m p) + h sin(m p)] dP (sin(phi))
			   n=1             m=0   n            n           n  */
			/* Equation 11 in the WMM Technical report. Derivative with respect to longitude, divided by radius. */
			MagneticResults->By +=
			    SphVariables.RelativeRadiusPower[n] *
			    (MagneticModel->Main_Field_Coeff_G[index] *
			    SphVariables.sin_mlambda[m] -
			    MagneticModel->Main_Field_Coeff_H[index] *
			    SphVariables.cos_mlambda[m])
			    * (double)(m) * LegendreFunction->Pcup[index];
			/*             nMax  (n+2) n     m            m           m
			   Bx = - SUM (a/r)   SUM  [g cos(m p) + h sin(m p)] dP (sin(phi))
			   n=1         m=0   n            n           n  */
			/* Equation 10  in the WMM Technical report. Derivative with respect to latitude, divided by radius. */

			MagneticResults->Bx -=
			    SphVariables.RelativeRadiusPower[n] *
			    (MagneticModel->Main_Field_Coeff_G[index] *
			    SphVariables.cos_mlambda[m] +
			    MagneticModel->Main_Field_Coeff_H[index] *
			    SphVariables.sin_mlambda[m])
			    * LegendreFunction->dPcup[index];

		}
	}

	cos_phi = cos(DEG2RAD(CoordSpherical.phig));
	if (fabs(cos_phi) > 1.0e-10) {
		MagneticResults->By = MagneticResults->By / cos_phi;
	} else
		/* Special calculation for component - By - at Geographic poles.
		 * If the user wants to avoid using this function,  please make sure that
		 * the latitude is not exactly +/-90. An option is to make use the function
		 * MAG_CheckGeographicPoles.
		 */
	{
		MAG_SummationSpecial(MagneticModel, SphVariables,
		    CoordSpherical, MagneticResults);
	}
	return (TRUE);
}				/*MAG_Summation */

int
MAG_SummationSpecial(MAGtype_MagneticModel * MagneticModel,
    MAGtype_SphericalHarmonicVariables SphVariables,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_MagneticResults * MagneticResults)
/* Special calculation for the component By at Geographic poles.
Manoj Nair, June, 2009 manoj.c.nair@noaa.gov
INPUT: MagneticModel
           SphVariables
           CoordSpherical
OUTPUT: MagneticResults
CALLS : none
See Section 1.4, "SINGULARITIES AT THE GEOGRAPHIC POLES", WMM Technical report

 */
{
	int n, index;
	double k, sin_phi, *PcupS, schmidtQuasiNorm1, schmidtQuasiNorm2,
	    schmidtQuasiNorm3;

	PcupS = (double *)malloc((MagneticModel->nMax + 1) * sizeof (double));
	if (PcupS == 0) {
		MAG_Error(14);
		return (FALSE);
	}

	PcupS[0] = 1;
	schmidtQuasiNorm1 = 1.0;

	MagneticResults->By = 0.0;
	sin_phi = sin(DEG2RAD(CoordSpherical.phig));

	for (n = 1; n <= MagneticModel->nMax; n++) {

		/*Compute the ration between the Gauss-normalized associated Legendre
		   functions and the Schmidt quasi-normalized version. This is equivalent to
		   sqrt((m==0?1:2)*(n-m)!/(n+m!))*(2n-1)!!/(n-m)!  */

		index = (n * (n + 1) / 2 + 1);
		schmidtQuasiNorm2 =
		    schmidtQuasiNorm1 * (double)(2 * n - 1) / (double)n;
		schmidtQuasiNorm3 =
		    schmidtQuasiNorm2 * sqrt((double)(n * 2) / (double)(n + 1));
		schmidtQuasiNorm1 = schmidtQuasiNorm2;
		if (n == 1) {
			PcupS[n] = PcupS[n - 1];
		} else {
			k = (double)(((n - 1) * (n - 1)) -
			    1) / (double)((2 * n - 1) * (2 * n - 3));
			PcupS[n] = sin_phi * PcupS[n - 1] - k * PcupS[n - 2];
		}

		/*                1 nMax  (n+2)    n     m            m           m
		   By =    SUM (a/r) (m)  SUM  [g cos(m p) + h sin(m p)] dP (sin(phi))
		   n=1             m=0   n            n           n  */
		/* Equation 11 in the WMM Technical report. Derivative with respect to longitude, divided by radius. */

		MagneticResults->By += SphVariables.RelativeRadiusPower[n] *
		    (MagneticModel->Main_Field_Coeff_G[index] *
		    SphVariables.sin_mlambda[1] -
		    MagneticModel->Main_Field_Coeff_H[index] *
		    SphVariables.cos_mlambda[1])
		    * PcupS[n] * schmidtQuasiNorm3;
	}

	if (PcupS)
		free(PcupS);
	return (TRUE);
}				/*MAG_SummationSpecial */

int
MAG_TimelyModifyMagneticModel(MAGtype_Date UserDate,
    MAGtype_MagneticModel *MagneticModel,
    MAGtype_MagneticModel *TimedMagneticModel)

/* Time change the Model coefficients from the base year of the model using secular variation coefficients.
Store the coefficients of the static model with their values advanced from epoch t0 to epoch t.
Copy the SV coefficients.  If input "t�" is the same as "t0", then this is merely a copy operation.
If the address of "TimedMagneticModel" is the same as the address of "MagneticModel", then this procedure overwrites
the given item "MagneticModel".

INPUT: UserDate
           MagneticModel
OUTPUT:TimedMagneticModel
CALLS : none
 */
{
	int n, m, index, a, b;
	TimedMagneticModel->EditionDate = MagneticModel->EditionDate;
	TimedMagneticModel->epoch = MagneticModel->epoch;
	TimedMagneticModel->CoefficientFileEndDate =
	    MagneticModel->CoefficientFileEndDate;
	TimedMagneticModel->nMax = MagneticModel->nMax;
	TimedMagneticModel->nMaxSecVar = MagneticModel->nMaxSecVar;
	a = TimedMagneticModel->nMaxSecVar;
	b = (a * (a + 1) / 2 + a);
	strcpy(TimedMagneticModel->ModelName, MagneticModel->ModelName);
	for (n = 1; n <= MagneticModel->nMax; n++) {
		for (m = 0; m <= n; m++) {
			index = (n * (n + 1) / 2 + m);
			if (index <= b) {
				TimedMagneticModel->Main_Field_Coeff_H[index] =
				    MagneticModel->Main_Field_Coeff_H[index] +
				    (UserDate.DecimalYear -
				    MagneticModel->epoch) *
				    MagneticModel->Secular_Var_Coeff_H[index];
				TimedMagneticModel->Main_Field_Coeff_G[index] =
				    MagneticModel->Main_Field_Coeff_G[index] +
				    (UserDate.DecimalYear -
				    MagneticModel->epoch) *
				    MagneticModel->Secular_Var_Coeff_G[index];
				/*
				 * We need a copy of the secular var coef to
				 * calculate secular change
				 */
				TimedMagneticModel->Secular_Var_Coeff_H[index] =
				    MagneticModel->Secular_Var_Coeff_H[index];
				TimedMagneticModel->Secular_Var_Coeff_G[index] =
				    MagneticModel->Secular_Var_Coeff_G[index];
			} else {
				TimedMagneticModel->Main_Field_Coeff_H[index] =
				    MagneticModel->Main_Field_Coeff_H[index];
				TimedMagneticModel->Main_Field_Coeff_G[index] =
				    MagneticModel->Main_Field_Coeff_G[index];
			}
		}
	}
	return (TRUE);
}				/* MAG_TimelyModifyMagneticModel */

/*End of Spherical Harmonic Functions*/
