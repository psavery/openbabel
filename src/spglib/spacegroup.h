/* spacegroup.h */
/* Copyright (C) 2008 Atsushi Togo */

#ifndef __spacegroup_H__
#define __spacegroup_H__

#include "bravais.h"
#include "cell.h"
#include "symmetry.h"
#include "mathfunc.h"

typedef struct {
  char international[10];
  char schoenflies[10];
} Pointgroup;

typedef struct {
  int number;
  char international_long[40];
  char international[20];
  char schoenflies[10];
  char bravais_symbol[2];
  int multi;
  Pointgroup pointgroup;
  double bravais_lattice[3][3];
} Spacegroup;

/* Last two figures of numbers are meaningful. The third figure is not used. */
enum {
  IDENTITY = 8,
  PURE_TRANSLATION = 7,
  SECONDARY_2_AXIS = 4,
  TERTIARY_2_AXIS = 21,
  PRIMARY_2_AXIS = 9,
  PRIMARY_2_1_AXIS = 20,
  PRIMARY_3_AXIS = 10,
  PRIMARY_3_3_1_3_2_AXIS = 110,
  PRIMARY_3_1_AXIS = 22,
  PRIMARY_3_2_AXIS = 23,
  PRIMARY_4_AXIS = 12,
  PRIMARY_4_2_AXIS = 25,
  PRIMARY_4_1_4_3_AXIS = 124,
  PRIMARY_4_1_AXIS = 24,
  PRIMARY_4_3_AXIS = 26,
  PRIMARY_6_AXIS = 14,
  PRIMARY_6_3_AXIS = 29,
  PRIMARY_6_2_AXIS = 28,
  PRIMARY_6_4_AXIS = 30,
  PRIMARY_6_1_AXIS = 27,
  PRIMARY_6_5_AXIS = 31,
  INVERSION = 5,
  PRIMARY_M3_AXIS = 3,
  PRIMARY_M4_AXIS = 2,
  PRIMARY_M6_AXIS = 1,
  M_PLANE = 15,
  PRIMARY_N = 18,
  SECONDARY_A_B = 16,
  SECONDARY_C = 17,
  SECONDARY_N = 118,
  TERTIARY_M = 115,
  TERTIARY_C = 19,
  A_B_C_PLANE = 116,
  TERTIARY_D = 117,
  SECONDARY_M = 215,
  SECONDARY_C_RHOMB = 216,
  PRIMARY_M = 315,
  TERTIARY_M_HEXA = 317,
  TERTIARY_C_HEXA = 418,
  SECONDARY_C_HEXA = 316,
  TERTIARY_N = 218,
  N_PLANE = 318,
  D_PLANE = 217,
  G_PLANE = 119
};

Spacegroup tbl_get_spacegroup( SPGCONST Cell * cell,
			       const double symprec );
Symmetry tbl_get_conventional_symmetry( SPGCONST Bravais * bravais,
					SPGCONST Cell * primitive,
					const Symmetry * primitive_sym,
					const double symprec );

#endif
