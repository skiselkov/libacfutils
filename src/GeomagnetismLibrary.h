/*	WMM Subroutine library was tested in the following environments
 *
 *	1. Red Hat Linux  with GCC Compiler
 *	2. MS Windows XP with CodeGear C++ compiler
 *	3. Sun Solaris with GCC Compiler
 *
 *
 *      Revision Number: $Revision: 1288 $
 *      Last changed by: $Author: awoods $
 *      Last changed on: $Date: 2014-12-09 16:43:07 -0700 (Tue, 09 Dec 2014) $
 *
 *
 */

/*
 #ifndef EPOCHRANGE
 #define EPOCHRANGE (int)5
 #endif
*/

#ifndef	GEOMAGHEADER_H
#define	GEOMAGHEADER_H

#ifndef	_POSIX_C_SOURCE
#define	_POSIX_C_SOURCE	200112L
#endif

#include <acfutils/geom.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	READONLYMODE "r"
#define	MAXLINELENGTH (1024)
#define	NOOFPARAMS (15)
#define	NOOFCOEFFICIENTS (7)

#define	_DEGREE_NOT_FOUND (-2)
#define	CALCULATE_NUMTERMS(N)    (N * ( N + 1 ) / 2 + N)

/*These error values come from the ISCWSA error model:
 *http://www.copsegrove.com/Pages/MWDGeomagneticModels.aspx
 */
#define	INCL_ERROR_BASE (0.20)
#define	DECL_ERROR_OFFSET_BASE (0.36)
#define	F_ERROR_BASE (130)
#define	DECL_ERROR_SLOPE_BASE (5000)
#define	WMM_ERROR_MULTIPLIER 1.21
#define	IGRF_ERROR_MULTIPLIER 1.21

#ifndef	M_PI
#define	M_PI    ((2)*(acos(0.0)))
#endif

#define	ATanH(x)	    (0.5 * log((1 + x) / (1 - x)))

#ifndef	TRUE
#define	TRUE            ((int)1)
#define	FALSE           ((int)0)
#endif

/*
Data types and prototype declaration for
World Magnetic Model (WMM) subroutines.

July 28, 2009

manoj.c.nair@noaa.gov*/

typedef struct {
	double EditionDate;
	/* Base time of Geomagnetic model epoch (yrs) */
	double epoch;
	double CoefficientFileEndDate;
	char ModelName[32];
	/*
	 * C - Gauss coefficients of main geomagnetic model (nT)
	 * Index is (n * (n + 1) / 2 + m)
	 */
	double *Main_Field_Coeff_G;
	/* C - Gauss coefficients of main geomagnetic model (nT) */
	double *Main_Field_Coeff_H;
	/* CD - Gauss coefficients of secular geomagnetic model (nT/yr) */
	double *Secular_Var_Coeff_G;
	/* CD - Gauss coefficients of secular geomagnetic model (nT/yr) */
	double *Secular_Var_Coeff_H;
	/* Maximum degree of spherical harmonic model */
	int nMax;
	/* Maximum degree of spherical harmonic secular model */
	int nMaxSecVar;
	/*
	 * Whether or not the magnetic secular variation vector will be
	 * needed by program
	 */
	int SecularVariationUsed;
} MAGtype_MagneticModel;

typedef struct {
	double a;		/*semi-major axis of the ellipsoid */
	double b;		/*semi-minor axis of the ellipsoid */
	double fla;		/* flattening */
	double epssq;		/*first eccentricity squared */
	double eps;		/* first eccentricity */
	double re;		/* mean radius of  ellipsoid */
} MAGtype_Ellipsoid;

typedef struct {
	double lambda;			/* longitude */
	double phi;			/* geodetic latitude */
	double HeightAboveEllipsoid;	/* height above the ellipsoid (HaE) */
	int UseGeoid;
} MAGtype_CoordGeodetic;

typedef struct {
	double lambda;		/* longitude */
	double phig;		/* geocentric latitude */
	double r;		/* distance from the center of the ellipsoid */
} MAGtype_CoordSpherical;

typedef struct {
	double DecimalYear;	/* decimal years */
} MAGtype_Date;

typedef struct {
	double Decl;		/* 1. Angle between the magnetic field vector and true north, positive east */
	double Incl;		/*2. Angle between the magnetic field vector and the horizontal plane, positive down */
	double F;		/*3. Magnetic Field Strength */
	double H;		/*4. Horizontal Magnetic Field Strength */
	double X;		/*5. Northern component of the magnetic field vector */
	double Y;		/*6. Eastern component of the magnetic field vector */
	double Z;		/*7. Downward component of the magnetic field vector */
	double GV;		/*8. The Grid Variation */
	double Decldot;		/*9. Yearly Rate of change in declination */
	double Incldot;		/*10. Yearly Rate of change in inclination */
	double Fdot;		/*11. Yearly rate of change in Magnetic field strength */
	double Hdot;		/*12. Yearly rate of change in horizontal field strength */
	double Xdot;		/*13. Yearly rate of change in the northern component */
	double Ydot;		/*14. Yearly rate of change in the eastern component */
	double Zdot;		/*15. Yearly rate of change in the downward component */
	double GVdot;		/*16. Yearly rate of change in grid variation */
} MAGtype_GeoMagneticElements;

typedef struct {
	int NumbGeoidCols;	/* 360 degrees of longitude at 15 minute spacing */
	int NumbGeoidRows;	/* 180 degrees of latitude  at 15 minute spacing */
	int NumbHeaderItems;	/* min, max lat, min, max long, lat, long spacing */
	int ScaleFactor;	/* 4 grid cells per degree at 15 minute spacing  */
	float *GeoidHeightBuffer;
	int NumbGeoidElevs;
	int Geoid_Initialized;	/* indicates successful initialization */
	int UseGeoid;		/*Is the Geoid being used? */
} MAGtype_Geoid;

typedef struct {
	double Easting;		/* (X) in meters */
	double Northing;	/* (Y) in meters */
	int Zone;		/*UTM Zone */
	char HemiSphere;
	double CentralMeridian;
	double ConvergenceOfMeridians;
	double PointScale;
} MAGtype_UTMParameters;

/*Prototypes */

/*Functions that should be Magnetic Model member functions*/

/*Wrapper Functions*/
int MAG_Geomag(MAGtype_Ellipsoid Ellip,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_CoordGeodetic CoordGeodetic,
    MAGtype_MagneticModel * TimedMagneticModel,
    MAGtype_GeoMagneticElements * GeoMagneticElements);

int MAG_robustReadMagModels(const char *filename,
    MAGtype_MagneticModel ** magneticmodel);

/*User Interface*/

void MAG_Error(int control);

/*Memory and File Processing*/

MAGtype_MagneticModel *MAG_AllocateModelMemory(int NumTerms);
int MAG_FreeMagneticModelMemory(MAGtype_MagneticModel *MagneticModel);

int MAG_readMagneticModel(const char *filename,
    MAGtype_MagneticModel * MagneticModel);

int MAG_readMagneticModel_Large(const char *filename, const char *filenameSV,
    MAGtype_MagneticModel * MagneticModel);

/*Conversions, Transformations, and other Calculations*/
int MAG_GeodeticToSpherical(MAGtype_Ellipsoid Ellip,
    MAGtype_CoordGeodetic CoordGeodetic,
    MAGtype_CoordSpherical * CoordSpherical);

void MAG_SphericalToGeodetic(MAGtype_Ellipsoid Ellip,
    MAGtype_CoordSpherical CoordSpherical,
    MAGtype_CoordGeodetic * CoordGeodetic);

/*Spherical Harmonics*/

int MAG_TimelyModifyMagneticModel(MAGtype_Date UserDate,
    MAGtype_MagneticModel * MagneticModel,
    MAGtype_MagneticModel * TimedMagneticModel);

#ifdef	__cplusplus
}
#endif

#endif				/*GEOMAGHEADER_H */
