/**********************************************************************
  Xtal - Wrapper for Structure to ease work with crystals.

  Copyright (C) 2009-2011 by David C. Lonie

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#include <xtalopt/structures/xtal.h>

#include <xtalopt/xtalopt.h>

#include <avogadro/primitive.h>
#include <avogadro/molecule.h>
#include <avogadro/atom.h>

#include <globalsearch/macros.h>
#include <globalsearch/obeigenconv.h>
#include <globalsearch/stablecomparison.h>

#include <openbabel/generic.h>
#include <openbabel/forcefield.h>

extern "C" {
#include <spglib/spglib.h>
}

#include <xtalcomp/xtalcomp.h>

#include <Eigen/LU>

#include <QtCore/QFile>
#include <QtCore/QDebug>
#include <QtCore/QRegExp>
#include <QtCore/QStringList>

#define DEBUG_MATRIX(m) printf("| %9.5f %9.5f %9.5f |\n"        \
                               "| %9.5f %9.5f %9.5f |\n"        \
                               "| %9.5f %9.5f %9.5f |\n",       \
                               (m)(0,0), (m)(0,1), (m)(0,2),    \
                               (m)(1,0), (m)(1,1), (m)(1,2),    \
                               (m)(2,0), (m)(2,1), (m)(2,2))

using namespace std;
using namespace OpenBabel;
using namespace Avogadro;
using namespace Eigen;

namespace XtalOpt {

  Xtal::Xtal(QObject *parent) :
    Structure(parent)
  {
    ctor(parent);
  }

  Xtal::Xtal(double A, double B, double C,
             double Alpha, double Beta, double Gamma,
             QObject *parent) :
  Structure(parent)
  {
    ctor(parent);
    setCellInfo(A, B, C, Alpha, Beta, Gamma);
  }

  // Actual constructor:
  void Xtal::ctor(QObject *parent)
  {
    if (!m_mixMatrices.size()) {
      generateValidCOBs();
    }
    m_spgNumber = 231;
    this->setOBUnitCell(new OpenBabel::OBUnitCell);
    // Openbabel seems to be fond of making unfounded assumptions that
    // break things. This fixes one of them.
    this->cell()->SetSpaceGroup(0);
    this->setParentStructure(NULL);
  }

  Xtal::~Xtal() {
  }

  void Xtal::setVolume(double  Volume) {

    // Get scaling factor
    double factor = pow(Volume/cell()->GetCellVolume(), 1.0/3.0); // Cube root

    // Store position of atoms in fractional units
    QList<Atom*> atomList       = atoms();
    QList<Vector3*> fracCoordsList;
    for (int i = 0; i < atomList.size(); i++)
      fracCoordsList.append(cartToFrac(atomList.at(i).pos()));

    // Scale cell
    setCellInfo(factor * cell()->GetA(),
                factor * cell()->GetB(),
                factor * cell()->GetC(),
                cell()->GetAlpha(),
                cell()->GetBeta(),
                cell()->GetGamma());

    // Recalculate coordinates:
    for (int i = 0; i < atomList.size(); i++)
      atomList.at(i)->setPos(fracToCart(*(fracCoordsList.at(i))));

    // Free memory
    qDeleteAll(fracCoordsList);
  }

  void Xtal::rescaleCell(double a, double b, double c,
                         double alpha, double beta, double gamma)
  {
    if (!a && !b && !c && !alpha && !beta && !gamma) {
      return;
    }

    this->rotateCellAndCoordsToStandardOrientation();

    // Store position of atoms in fractional units
    QList<Atom*> atomList       = atoms();
    QList<Vector3*> fracCoordsList;
    for (int i = 0; i < atomList.size(); i++)
      fracCoordsList.append(cartToFrac(atomList.at(i).pos()));

    double nA = getA();
    double nB = getB();
    double nC = getC();
    double nAlpha = getAlpha();
    double nBeta = getBeta();
    double nGamma = getGamma();

    // Set angles and reset volume
    if (alpha || beta || gamma) {
      double volume = getVolume();
      if (alpha) nAlpha = alpha;
      if (beta) nBeta = beta;
      if (gamma) nGamma = gamma;
      setCellInfo(nA,
                  nB,
                  nC,
                  nAlpha,
                  nBeta,
                  nGamma);
      setVolume(volume);
    }

    if (a || b || c) {
      // Initialize variables
      double scale_primary, scale_secondary, scale_tertiary;

      // Set lengths while preserving volume
      // Case one length is static
      if (a && !b && !c) {        // A
        scale_primary = a / nA;
        scale_secondary = pow(scale_primary, 0.5);
        nA = a;
        nB *= scale_secondary;
        nC *= scale_secondary;
      }
      else if (!a && b && !c) {   // B
        scale_primary = b / nB;
        scale_secondary = pow(scale_primary, 0.5);
        nB = b;
        nA *= scale_secondary;
        nC *= scale_secondary;
      }
      else if (!a && !b && c) {   // C
        scale_primary = c / nC;
        scale_secondary = pow(scale_primary, 0.5);
        nC = c;
        nA *= scale_secondary;
        nB *= scale_secondary;
      }
      // Case two lengths are static
      else if (a && b && !c) {    // AB
        scale_primary   = a / nA;
        scale_secondary = b / nB;
        scale_tertiary  = scale_primary * scale_secondary;
        nA = a;
        nB = b;
        nC *= scale_tertiary;
      }
      else if (a && !b && c) {    // AC
        scale_primary   = a / nA;
        scale_secondary = c / nC;
        scale_tertiary  = scale_primary * scale_secondary;
        nA = a;
        nC = c;
        nB *= scale_tertiary;
      }
      else if (!a && b && c) {    // BC
        scale_primary   = c / nC;
        scale_secondary = b / nB;
        scale_tertiary  = scale_primary * scale_secondary;
        nC = c;
        nB = b;
        nA *= scale_tertiary;
      }
      // Case three lengths are static
      else if (a && b && c) {     // ABC
        nA = a;
        nB = b;
        nC = c;
      }

      // Update unit cell
      setCellInfo(nA,
                  nB,
                  nC,
                  nAlpha,
                  nBeta,
                  nGamma);
    }

    // Recalculate coordinates:
    for (int i = 0; i < atomList.size(); i++)
      atomList.at(i)->setPos(fracToCart(fracCoordsList.at(i)));
  }

  bool Xtal::niggliReduce(const unsigned int iterations, double lenTol)
  {
    // Cache volume for later sanity checks
    const double origVolume = cell()->GetCellVolume();

    // Grab lattice vectors
    const std::vector<OpenBabel::vector3> obvecs = cell()->GetCellVectors();
    const Vector3 v1 (OB2Eigen(obvecs[0]));
    const Vector3 v2 (OB2Eigen(obvecs[1]));
    const Vector3 v3 (OB2Eigen(obvecs[2]));

    // Compute characteristic (step 0)
    double A    = v1.squaredNorm();
    double B    = v2.squaredNorm();
    double C    = v3.squaredNorm();
    double xi   = 2*v2.dot(v3);
    double eta  = 2*v1.dot(v3);
    double zeta = 2*v1.dot(v2);

    // Return value
    bool ret = false;

    // comparison tolerance
    double tol = 0.001 * lenTol * pow(this->getVolume(), 2.0/3.0);

    // Initialize change of basis matrices:
    //
    // Although the reduction algorithm produces quantities directly
    // relatible to a,b,c,alpha,beta,gamma, we will calculate a change
    // of basis matrix to use instead, and discard A, B, C, xi, eta,
    // zeta. By multiplying the change of basis matrix against the
    // current cell matrix, we avoid the problem of handling the
    // orientation matrix already present in the cell. The inverse of
    // this matrix can also be used later to convert the atomic
    // positions.
    // tmpMat is used to build other matrices
    Matrix3 tmpMat;

    // Cache static matrices:

    // Swap x,y (Used in Step 1). Negatives ensure proper sign of final
    // determinant.
    tmpMat << 0,-1,0, -1,0,0, 0,0,-1;
    const Matrix3 C1(tmpMat);
    // Swap y,z (Used in Step 2). Negatives ensure proper sign of final
    // determinant
    tmpMat << -1,0,0, 0,0,-1, 0,-1,0;
    const Matrix3 C2(tmpMat);
    // For step 8:
    tmpMat << 1,0,1, 0,1,1, 0,0,1;
    const Matrix3 C8(tmpMat);

    // initial change of basis matrix
    tmpMat << 1,0,0, 0,1,0, 0,0,1;
    Matrix3 cob(tmpMat);

    // Enable debugging output here:
//#define NIGGLI_DEBUG(step) qDebug() << iter << step << A << B << C << xi << eta << zeta << cob.determinant(); \
//std::cout << cob << std::endl;
#define NIGGLI_DEBUG(step)

    unsigned int iter;
    for (iter = 0; iter < iterations; ++iter) {
      Q_ASSERT(fabs(cob.determinant() - 1.0) < 1e-5);
      // Step 1:
      if (
          StableComp::gt(A, B, tol)
          || (
              StableComp::eq(A, B, tol)
              &&
              StableComp::gt(fabs(xi), fabs(eta), tol)
              )
          ) {
        cob *= C1;
        qSwap(A, B);
        qSwap(xi, eta);
        NIGGLI_DEBUG(1);
        ++iter;
      }

      // Step 2:
      if (
          StableComp::gt(B, C, tol)
          || (
              StableComp::eq(B, C, tol)
              &&
              StableComp::gt(fabs(eta), fabs(zeta), tol)
              )
          ) {
        cob *= C2;
        qSwap(B, C);
        qSwap(eta, zeta);
        NIGGLI_DEBUG(2);
        continue;
      }

      // Step 3:
      // Use exact comparisons in steps 3 and 4.
      if (xi*eta*zeta > 0) {
        // Update change of basis matrix:
        tmpMat <<
           StableComp::sign(xi),0,0,
           0,StableComp::sign(eta),0,
           0,0,StableComp::sign(zeta);
        cob *= tmpMat;

        // Update characteristic
        xi   = fabs(xi);
        eta  = fabs(eta);
        zeta = fabs(zeta);
        NIGGLI_DEBUG(3);
        ++iter;
      }

      // Step 4:
      // Use exact comparisons for steps 3 and 4
      else { // either step 3 or 4 should run
        // Update change of basis matrix:
        double *p = NULL;
        double i = 1;
        double j = 1;
        double k = 1;
        if (xi > 0) {
          i = -1;
        }
        else if (!(xi < 0)) {
          p = &i;
        }
        if (eta > 0) {
          j = -1;
        }
        else if (!(eta < 0)) {
          p = &j;
        }
        if (zeta > 0) {
          k = -1;
        }
        else if (!(zeta < 0)) {
          p = &k;
        }
        if (i*j*k < 0) {
          if (!p) {
            std::cerr << "XtalComp warning: one of the input structures "
                         "contains a lattice that is confusing the Niggli "
                         "reduction algorithm. Try making a small perturbation "
                         "(approx. 2 orders of magnitude smaller than the "
                         "tolerance) to the input lattices and try again. The "
                         "results of this comparison should not be relied upon."
                         "\n";
            return false;
          }
          *p = -1;
        }
        tmpMat <<i,0,0, 0,j,0, 0,0,k;
        cob *= tmpMat;

        // Update characteristic
        xi   = -fabs(xi);
        eta  = -fabs(eta);
        zeta = -fabs(zeta);
        NIGGLI_DEBUG(4);
        ++iter;
      }

      // Step 5:
      if (StableComp::gt(fabs(xi), B, tol)
          || (StableComp::eq(xi, B, tol)
              && StableComp::lt(2*eta, zeta, tol)
              )
          || (StableComp::eq(xi, -B, tol)
              && StableComp::lt(zeta, 0, tol)
              )
          ) {
        double signXi = StableComp::sign(xi);
        // Update change of basis matrix:
        tmpMat << 1,0,0, 0,1,-signXi, 0,0,1;
        cob *= tmpMat;

        // Update characteristic
        C    = B + C - xi*signXi;
        eta  = eta - zeta*signXi;
        xi   = xi -   2*B*signXi;
        NIGGLI_DEBUG(5);
        continue;
      }

      // Step 6:
      if (StableComp::gt(fabs(eta), A, tol)
          || (StableComp::eq(eta, A, tol)
              && StableComp::lt(2*xi, zeta, tol)
              )
          || (StableComp::eq(eta, -A, tol)
              && StableComp::lt(zeta, 0, tol)
              )
          ) {
        double signEta = StableComp::sign(eta);
        // Update change of basis matrix:
        tmpMat << 1,0,-signEta, 0,1,0, 0,0,1;
        cob *= tmpMat;

        // Update characteristic
        C    = A + C - eta*signEta;
        xi   = xi - zeta*signEta;
        eta  = eta - 2*A*signEta;
        NIGGLI_DEBUG(6);
        continue;
      }

      // Step 7:
      if (StableComp::gt(fabs(zeta), A, tol)
          || (StableComp::eq(zeta, A, tol)
              && StableComp::lt(2*xi, eta, tol)
              )
          || (StableComp::eq(zeta, -A, tol)
              && StableComp::lt(eta, 0, tol)
              )
          ) {
        double signZeta = StableComp::sign(zeta);
        // Update change of basis matrix:
        tmpMat << 1,-signZeta,0, 0,1,0, 0,0,1;
        cob *= tmpMat;

        // Update characteristic
        B    = A + B - zeta*signZeta;
        xi   = xi - eta*signZeta;
        zeta = zeta - 2*A*signZeta;
        NIGGLI_DEBUG(7);
        continue;
      }

      // Step 8:
      double sumAllButC = A + B + xi + eta + zeta;
      if (StableComp::lt(sumAllButC, 0, tol)
          || (StableComp::eq(sumAllButC, 0, tol)
              && StableComp::gt(2*(A+eta)+zeta, 0, tol)
              )
          ) {
        // Update change of basis matrix:
        cob *= C8;

        // Update characteristic
        C    = sumAllButC + C;
        xi = 2*B + xi + zeta;
        eta  = 2*A + eta + zeta;
        NIGGLI_DEBUG(8);
        continue;
      }

      // Done!
      NIGGLI_DEBUG(999);
      ret = true;
      break;
    }

    // No change, already reduced. Just return.
    if (iter == 0) {
      return true;
    }

    // iterations exceeded
    if (!ret) {
      return false;
    }

    Q_ASSERT_X(cob.determinant() == 1, Q_FUNC_INFO,
               "Determinant of change of basis matrix must be 1.");

    // Update cell
    setCellInfo( Eigen2OB(cob).transpose() * cell()->GetCellMatrix());

    // Check that volume has not changed
    Q_ASSERT_X(StableComp::eq(origVolume, cell()->GetCellVolume(), tol),
               Q_FUNC_INFO, "Cell volume changed during Niggli reduction.");

    // Rotate and wrap
    rotateCellAndCoordsToStandardOrientation();
    wrapAtomsToCell();
    return true;
  }

  bool Xtal::isNiggliReduced(double lenTol) const
  {
    // cache params
    double a     = getA();
    double b     = getB();
    double c     = getC();
    double alpha = getAlpha();
    double beta  = getBeta();
    double gamma = getGamma();

    return Xtal::isNiggliReduced(a, b, c, alpha, beta, gamma, lenTol);
  }

  bool Xtal::isNiggliReduced(const double a, const double b, const double c,
                             const double alpha, const double beta,
                             const double gamma, double lenTol)
  {
    // Calculate characteristic
    double A    = a*a;
    double B    = b*b;
    double C    = c*c;
    double xi   = 2*b*c*cos(alpha * DEG_TO_RAD);
    double eta  = 2*a*c*cos(beta * DEG_TO_RAD);
    double zeta = 2*a*b*cos(gamma * DEG_TO_RAD);

    // comparison tolerance
    // This may not be exactly the same as pow(origVolume, 2.0/3.0), but we'll
    // say that it's close enough...
    double tol = 0.001 * lenTol * pow( (a + b + c) / 3.0, 2.0);

    // First check the Buerger conditions. Taken from: Gruber B.. Acta
    // Cryst. A. 1973;29(4):433-440. Available at:
    // http://scripts.iucr.org/cgi-bin/paper?S0567739473001063
    // [Accessed December 15, 2010].
    if (StableComp::gt(A, B, tol) || StableComp::gt(B, C, tol)) return false;
    if (StableComp::eq(A, B, tol) && StableComp::gt(fabs(xi), fabs(eta), tol)) return false;
    if (StableComp::eq(B, C, tol) && StableComp::gt(fabs(eta), fabs(zeta), tol)) return false;
    if ( !(StableComp::gt(xi, 0.0, tol) &&
           StableComp::gt(eta, 0.0, tol) &&
           StableComp::gt(zeta, 0.0, tol))
         &&
         !(StableComp::leq(zeta, 0.0, tol) &&
           StableComp::leq(zeta, 0.0, tol) &&
           StableComp::leq(zeta, 0.0, tol)) ) return false;

    // Check against Niggli conditions (taken from Gruber 1973). The
    // logic of the second comparison is reversed from the paper to
    // simplify the algorithm.
    if (StableComp::eq(xi,    B, tol) && StableComp::gt (zeta, 2*eta,  tol)) return false;
    if (StableComp::eq(eta,   A, tol) && StableComp::gt (zeta, 2*xi,   tol)) return false;
    if (StableComp::eq(zeta,  A, tol) && StableComp::gt (eta,  2*xi,   tol)) return false;
    if (StableComp::eq(xi,   -B, tol) && StableComp::neq(zeta, 0,      tol)) return false;
    if (StableComp::eq(eta,  -A, tol) && StableComp::neq(zeta, 0,      tol)) return false;
    if (StableComp::eq(zeta, -A, tol) && StableComp::neq(eta,  0,      tol)) return false;

    if (StableComp::eq(xi+eta+zeta+A+B, 0, tol)
        && StableComp::gt(2*(A+eta)+zeta,  0, tol)) return false;

    // all good!
    return true;
  }

  bool Xtal::isPrimitive(const double cartTol) {

    // Cache fractional coordinates and atomic nums
    QList<Vector3> fcoords;
    QList<unsigned int> atomicNums;
    for (QList<Atom*>::const_iterator it = m_atomList.constBegin(),
           it_end = m_atomList.constEnd(); it != it_end; ++it) {
      fcoords.append( cartToFrac(*(*it).pos()));
      atomicNums.append((*it).atomicNumber());
    }
    size_t originalFCoordsSize = fcoords.size();

    // Get unit cell
    matrix3x3 obcell = this->OBUnitCell()->GetCellMatrix();
    // Convert to Eigen:
    Matrix3 cellMatrix = Matrix3::Zero();
    for (int row = 0; row < 3; row++) {
      for (int col = 0; col < 3; col++) {
        cellMatrix(row,col) = obcell.Get(row, col);
      }
    }

    unsigned int spg = reduceToPrimitive(&fcoords, &atomicNums,
                                         &cellMatrix, cartTol);

    if (originalFCoordsSize == fcoords.size()) return true;
    else return false;
  }


  bool Xtal::reduceToPrimitive(const double cartTol) {

    // Cache fractional coordinates and atomic nums
    QList<Vector3> fcoords;
    QList<unsigned int> atomicNums;
    for (QList<Atom*>::const_iterator it = m_atomList.constBegin(),
           it_end = m_atomList.constEnd(); it != it_end; ++it) {
      fcoords.append( cartToFrac(*(*it).pos()));
      atomicNums.append((*it).atomicNumber());
    }

    // Get unit cell
    matrix3x3 obcell = this->OBUnitCell()->GetCellMatrix();
    // Convert to Eigen:
    Matrix3 cellMatrix = Matrix3::Zero();
    for (int row = 0; row < 3; row++) {
      for (int col = 0; col < 3; col++) {
        cellMatrix(row,col) = obcell.Get(row, col);
      }
    }

    unsigned int spg = reduceToPrimitive(&fcoords, &atomicNums,
                                         &cellMatrix, cartTol);

    // spg == 0 implies that reduceToPrimitive() failed
    if (spg == 0) return false;

    setCellInfo(Eigen2OB(cellMatrix));

    // Remove all atoms to simplify the change
    QList<Atom*> atomList = this->atoms();
    for (size_t i = 0; i < atomList.size(); i++)
                                            this->removeAtom(atomList.at(i));

    // Add the atoms in
    for (size_t i = 0; i < fcoords.size(); i++) {
        Atom* newAtom = this->addAtom();
        newAtom->setAtomicNumber(atomicNums.at(i));
        newAtom->setPos(fracToCart(fcoords.at(i)));
    }

    Q_ASSERT(fcoords.size() == atomicNums.size());
    Q_ASSERT(this->m_atomList.size() == fcoords.size());
    this->setFormulaUnits(0);

    return true;
  }

  unsigned int Xtal::reduceToPrimitive(QList<Vector3> *fcoords,
                                       QList<unsigned int> *atomicNums,
                                       Matrix3 *cellMatrix,
                                       const double cartTol)
  {
    Q_ASSERT(fcoords->size() == atomicNums->size());

    const int numAtoms = fcoords->size();

    if (numAtoms < 1) {
      qWarning() << "Cannot determine spacegroup of empty cell.";
      return 0;
    }

    // Spglib expects column vecs, so fill with transpose
    double lattice[3][3] = {
      {(*cellMatrix)(0,0), (*cellMatrix)(1,0), (*cellMatrix)(2,0)},
      {(*cellMatrix)(0,1), (*cellMatrix)(1,1), (*cellMatrix)(2,1)},
      {(*cellMatrix)(0,2), (*cellMatrix)(1,2), (*cellMatrix)(2,2)}
    };

    // Build position list. Include space for 4*numAtoms for the
    // cell refinement
    double (*positions)[3] = new double[4*numAtoms][3];
    int *types = new int[4*numAtoms];
    const Vector3 * fracCoord;
    for (int i = 0; i < numAtoms; ++i) {
      fracCoord         = &(*fcoords)[i];
      types[i]          = (*atomicNums)[i];
      positions[i][0]   = fracCoord->x();
      positions[i][1]   = fracCoord->y();
      positions[i][2]   = fracCoord->z();
    }

    // find spacegroup for return value
    char symbol[21];
    int spg = spg_get_international(symbol,
                                    lattice,
                                    positions,
                                    types,
                                    numAtoms,
                                    cartTol);

    // Refine the structure
    int numBravaisAtoms =
      spg_refine_cell(lattice, positions, types,
                      numAtoms, cartTol);

    // if spglib cannot refine the cell, return 0.
    if (numBravaisAtoms <= 0) {
      return 0;
    }

    // Find primitive cell. This updates lattice, positions, types
    // to primitive
    int numPrimitiveAtoms =
      spg_find_primitive(lattice, positions, types,
                         numBravaisAtoms, cartTol);

    // If the cell was already a primitive cell, reset
    // numPrimitiveAtoms.
    if (numPrimitiveAtoms == 0) {
      numPrimitiveAtoms = numBravaisAtoms;
    }

    // Bail if everything failed
    if (numPrimitiveAtoms <= 0) {
      return 0;
    }

    // Update passed objects
    // convert col vecs to row vecs
    (*cellMatrix)(0, 0) =  lattice[0][0];
    (*cellMatrix)(0, 1) =  lattice[1][0];
    (*cellMatrix)(0, 2) =  lattice[2][0];
    (*cellMatrix)(1, 0) =  lattice[0][1];
    (*cellMatrix)(1, 1) =  lattice[1][1];
    (*cellMatrix)(1, 2) =  lattice[2][1];
    (*cellMatrix)(2, 0) =  lattice[0][2];
    (*cellMatrix)(2, 1) =  lattice[1][2];
    (*cellMatrix)(2, 2) =  lattice[2][2];

    // Trim
    while (fcoords->size() > numPrimitiveAtoms) {
      fcoords->removeLast();
      atomicNums->removeLast();
    }
    while (fcoords->size() < numPrimitiveAtoms) {
      fcoords->append(Vector3());
      atomicNums->append(0);
    }

    // Update
    Q_ASSERT(fcoords->size() == atomicNums->size());
    Q_ASSERT(fcoords->size() == numPrimitiveAtoms);
    for (int i = 0; i < numPrimitiveAtoms; ++i) {
      (*atomicNums)[i]  = types[i];
      (*fcoords)[i] = Vector3 (positions[i]);
    }

    delete [] positions;
    delete [] types;

    if (spg > 230 || spg < 0) {
      spg = 0;
    }

    return static_cast<unsigned int>(spg);
  }

  QList<QString> Xtal::currentAtomicSymbols()
  {
    QList<QString> result;
    QList<GlobalSearch::Atom*> atoms = this->atoms();

    for (QList<GlobalSearch::Atom*>::const_iterator
           it = atoms.constBegin(),
           it_end = atoms.constEnd();
         it != it_end;
         ++it) {
      result <<
        OpenBabel::etab.GetSymbol((*it).atomicNumber());
    }
    return result;
  }

  inline void Xtal::updateMolecule(const QList<QString> &ids,
                                   const QList<Vector3> &coords)
  {
    // Remove old atoms
    // We should lock the xtal before calling this function!
    //QWriteLocker locker (this->lock());
    QList<GlobalSearch::Atom*> atoms = this->atoms();
    for (QList<GlobalSearch::Atom*>::iterator
           it = atoms.begin(),
           it_end = atoms.end();
         it != it_end;
         ++it) {
      this->removeAtom(*it);
    }

    // Add new atoms
    for (int i = 0; i < ids.size(); ++i) {
      Atom *atom = this->addAtom();
      atom->setAtomicNumber(OpenBabel::etab.GetAtomicNum
                            (ids[i].toStdString().c_str()));
      atom->setPos(coords[i]);
    }
  }

  void Xtal::setCurrentFractionalCoords(const QList<QString> &ids,
                                        const QList<Vector3> &fcoords)
  {
    OpenBabel::OBUnitCell *cell = this->cell();
    QList<Vector3> coords;
#if QT_VERSION >= 0x040700
    coords.reserve(fcoords.size());
#endif

    for (QList<Vector3>::const_iterator
           it = fcoords.constBegin(),
           it_end = fcoords.constEnd();
         it != it_end;
         ++it) {
      // Convert to storage cartesian
      coords.append(OB2Eigen(cell->FractionalToCartesian
                             (Eigen2OB(*it))));
    }

    updateMolecule(ids, coords);
  }

  void Xtal::printLatticeInfo() const
  {
    cout << "a is " << this->getA() << "\n";
    cout << "b is " << this->getB() << "\n";
    cout << "c is " << this->getC() << "\n";
    cout << "alpha is " << this->getAlpha() << "\n";
    cout << "beta is " << this->getBeta() << "\n";
    cout << "gamma is " << this->getGamma() << "\n";
    cout << "volume is " << this->getVolume() << "\n";
  }

  void Xtal::printAtomInfo() const
  {
    cout << "Frac coords info (blank if none):\n";
    QList<GlobalSearch::Atom*> atoms = this->atoms();
    QList<Vector3> fracCoords;

    for (size_t i = 0; i < atoms.size(); i++)
      fracCoords.append(*(cartToFrac(atoms.at(i).pos())));

    for (size_t i = 0; i < atoms.size(); i++) {
      cout << "  For atomic num " <<  atoms.at(i).atomicNumber() << ", coords are (" << fracCoords.at(i)[0] << "," << fracCoords.at(i)[1] << "," << fracCoords.at(i)[2] << ")\n";
    }
  }

  void Xtal::printXtalInfo() const
  {
    printLatticeInfo();
    printAtomInfo();
  }

  // Adapted from unitcellextension:
  void Xtal::fillUnitCell(uint spg, double cartTol)
  {
    const OpenBabel::SpaceGroup *sg = SpaceGroup::GetSpaceGroup(spg);
    if (!sg)
      return; // nothing to do

    wrapAtomsToCell();

    QList<Vector3> origFCoords = getAtomCoordsFrac();
    QList<Vector3> newFCoords;

    QList<QString> origIds = currentAtomicSymbols();
    QList<QString> newIds;

    // Duplicate tolerance squared
    const double dupTolSquared = cartTol * cartTol;

    // Non-fatal assert -- if the number of atoms has
    // changed, just tail-recurse and try again.
    if (origIds.size() != origFCoords.size()) {
      return fillUnitCell(spg);
    }

    const QString *curId;
    const Vector3 *curVec;
    std::list<OpenBabel::vector3> obxformed;
    std::list<OpenBabel::vector3>::const_iterator obxit;
    std::list<OpenBabel::vector3>::const_iterator obxit_end;
    QList<Vector3> xformed;
    QList<Vector3>::const_iterator xit, xit_end;
    QList<Vector3>::const_iterator newit, newit_end;
    for (int i = 0; i < origIds.size(); ++i) {
      curId = &origIds[i];
      curVec = &origFCoords[i];

      // Round off to remove floating point math errors
      double x = StableComp::round(curVec->x(), 7);
      double y = StableComp::round(curVec->y(), 7);
      double z = StableComp::round(curVec->z(), 7);

      // Get tranformed OB vectors
      obxformed = sg->Transform(OpenBabel::vector3(x,y,z));

      // Convert to Eigen, wrap to cell
      xformed.clear();
      Vector3 tmp;
      obxit_end = obxformed.end();
      for (obxit = obxformed.begin();
           obxit != obxit_end; ++obxit) {
        tmp = OB2Eigen(*obxit);
        // Pseudo-modulus
        tmp.x() -= static_cast<int>(tmp.x());
        tmp.y() -= static_cast<int>(tmp.y());
        tmp.z() -= static_cast<int>(tmp.z());
        // Correct negative values
        if (tmp.x() < 0.0) ++tmp.x();
        if (tmp.y() < 0.0) ++tmp.y();
        if (tmp.z() < 0.0) ++tmp.z();
        // Add a fudge factor for cell edges
        if (tmp.x() >= 1.0 - 1e-6) tmp.x() = 0.0;
        if (tmp.y() >= 1.0 - 1e-6) tmp.y() = 0.0;
        if (tmp.z() >= 1.0 - 1e-6) tmp.z() = 0.0;
        xformed.append(tmp);
      }

      // Check all xformed vectors against the coords
      // already added. if they match, skip this atom.
      bool duplicate;
      xit_end = xformed.constEnd();
      for (xit = xformed.constBegin();
           xit != xit_end; ++xit) {
        newit_end = newFCoords.constEnd();
        duplicate = false;
        for (newit = newFCoords.constBegin();
             newit != newit_end; ++newit) {
          if (fabs((*newit - *xit).squaredNorm())
              < dupTolSquared) {
            duplicate = true;
            break;
          }
        }

        if (duplicate) {
          continue;
        }

        // Add transformed atom
        newFCoords.append(*xit);
        newIds.append(*curId);
      }
    }

    setCurrentFractionalCoords(newIds, newFCoords);
  }

  // Inverse of fillUnitCell()
  void Xtal::reduceToAsymmetricUnit(double cartTol)
  {
    OpenBabel::OBUnitCell *cell = this->cell();
    if (!cell)
      return;
    const OpenBabel::SpaceGroup *sg = cell->GetSpaceGroup();
    if (!sg)
      return; // nothing to do

    wrapAtomsToCell();

    QList<Vector3> FCoords = getAtomCoordsFrac();
    QList<QString> Ids = currentAtomicSymbols();

    // Duplicate tolerance squared
    const double dupTolSquared = cartTol*cartTol;

    // Non-fatal assert -- if the number of atoms has
    // changed, just tail-recurse and try again.
    if (Ids.size() != FCoords.size()) {
      return reduceToAsymmetricUnit();
    }

    const Vector3 *curVec;
    std::list<OpenBabel::vector3> obxformed;
    std::list<OpenBabel::vector3>::const_iterator obxit;
    std::list<OpenBabel::vector3>::const_iterator obxit_end;
    QList<Vector3> xformed;
    QList<Vector3>::const_iterator xit, xit_end;

    // This loop modifies Ids and Fcoords, but only by removing
    // atoms for j > i.
    for (int i = 0; i < Ids.size(); ++i) {
      // Get tranformed OB vectors
      curVec = &FCoords[i];
      obxformed = sg->Transform(Eigen2OB(*curVec));

      // Convert to Eigen, wrap to cell
      xformed.clear();
      Vector3 tmp;
      obxit_end = obxformed.end();
      for (obxit = obxformed.begin();
           obxit != obxit_end; ++obxit) {
        tmp = OB2Eigen(*obxit);
        // Pseudo-modulus
        tmp.x() -= static_cast<int>(tmp.x());
        tmp.y() -= static_cast<int>(tmp.y());
        tmp.z() -= static_cast<int>(tmp.z());
        // Correct negative values
        if (tmp.x() < 0.0) ++tmp.x();
        if (tmp.y() < 0.0) ++tmp.y();
        if (tmp.z() < 0.0) ++tmp.z();
        // Add a fudge factor for cell edges
        if (tmp.x() >= 1.0 - 1e-6) tmp.x() = 0.0;
        if (tmp.y() >= 1.0 - 1e-6) tmp.y() = 0.0;
        if (tmp.z() >= 1.0 - 1e-6) tmp.z() = 0.0;
        xformed.append(tmp);
      }

      // Check which of the remaining atoms are equivalent to the current
      // atom and remove them.
      xit_end = xformed.constEnd();
      for (xit = xformed.constBegin();
           xit != xit_end; ++xit) {
        for (int j = i + 1; j < Ids.size(); j++) {
          if ((FCoords[j] - *xit).squaredNorm() < dupTolSquared) {
            FCoords.removeAt(j);
            Ids.removeAt(j);
          }
        }
      }
    }

    setCurrentFractionalCoords(Ids, FCoords);
  }

  bool Xtal::fixAngles(int attempts)
  {
    // Perform niggli reduction
    if (!niggliReduce(attempts)) {
      qDebug() << "Unable to perform cell reduction on Xtal " << getIDString()
               << "( " << getA() << getB() << getC()
               << getAlpha() << getBeta() << getGamma() << " )";
      return false;
    }

    findSpaceGroup();
    return true;
  }

  bool Xtal::operator==(const Xtal &o) const
  {
    // Compare coordinates using the default tolerance
    if (!compareCoordinates(o))
      return false;

    return true;
  }

  bool Xtal::compareCoordinates(const Xtal &other, const double lengthTol,
                                const double angleTol) const
  {
    // Cell matrices as row vectors
    const OpenBabel::matrix3x3 thisCellOB (this->cell()->GetCellMatrix());
    const OpenBabel::matrix3x3 otherCellOB (other.cell()->GetCellMatrix());
    XcMatrix thisCell (thisCellOB(0,0), thisCellOB(0,1), thisCellOB(0,2),
                       thisCellOB(1,0), thisCellOB(1,1), thisCellOB(1,2),
                       thisCellOB(2,0), thisCellOB(2,1), thisCellOB(2,2));
    XcMatrix otherCell(otherCellOB(0,0), otherCellOB(0,1), otherCellOB(0,2),
                       otherCellOB(1,0), otherCellOB(1,1), otherCellOB(1,2),
                       otherCellOB(2,0), otherCellOB(2,1), otherCellOB(2,2));

    // vectors of fractional coordinates and atomic numbers
    std::vector<XcVector> thisCoords;
    std::vector<XcVector> otherCoords;
    std::vector<unsigned int> thisTypes;
    std::vector<unsigned int> otherTypes;
    thisCoords.reserve(this->numAtoms());
    thisTypes.reserve(this->numAtoms());
    otherCoords.reserve(other.numAtoms());
    otherTypes.reserve(other.numAtoms());
    Vector3 pos;
    for (QList<Atom*>::const_iterator it = this->m_atomList.constBegin(),
           it_end = this->m_atomList.constEnd(); it != it_end; ++it) {
      pos = this->cartToFrac(*(*it).pos());
      thisCoords.push_back(XcVector(pos.x(), pos.y(), pos.z()));
      thisTypes.push_back((*it).atomicNumber());
    }
    for (QList<Atom*>::const_iterator it = other.m_atomList.constBegin(),
           it_end = other.m_atomList.constEnd(); it != it_end; ++it) {
      pos = other.cartToFrac(*(*it).pos());
      otherCoords.push_back(XcVector(pos.x(), pos.y(), pos.z()));
      otherTypes.push_back((*it).atomicNumber());
    }

    return XtalComp::compare(thisCell,  thisTypes,  thisCoords,
                             otherCell, otherTypes, otherCoords,
                             NULL, lengthTol, angleTol);
  }

  OpenBabel::OBUnitCell* Xtal::cell() const
  {
    return OBUnitCell();
  }

  bool Xtal::addAtomRandomly(uint atomicNumber, double minIAD, double maxIAD, int maxAttempts, Atom **atom) {
    INIT_RANDOM_GENERATOR();
    Q_UNUSED(maxIAD);

    double IAD = -1;
    int i = 0;
    vector3 cartCoords;

    // For first atom, add to 0, 0, 0
    if (numAtoms() == 0) {
      cartCoords = vector3 (0,0,0);
    }
    else {
      do {
        // Generate fractional coordinates
        IAD = -1;
        double x = RANDDOUBLE();
        double y = RANDDOUBLE();
        double z = RANDDOUBLE();

        // Convert to cartesian coordinates and store
        vector3 fracCoords (x,y,z);
        cartCoords = fracToCart(fracCoords);
        if (minIAD != -1) {
          getNearestNeighborDistance(cartCoords[0],
                                     cartCoords[1],
                                     cartCoords[2],
                                     IAD);
        }
        else { break;};
        i++;
      } while (i < maxAttempts && IAD <= minIAD);

      if (i >= maxAttempts) return false;
    }
    Atom *atm = addAtom();
    atom = &atm;
    Vector3 pos (cartCoords[0],cartCoords[1],cartCoords[2]);
    (*atom)->setPos(pos);
    (*atom)->setAtomicNumber(static_cast<int>(atomicNumber));
    return true;
  }

  bool Xtal::addAtomRandomly(
      unsigned int atomicNumber,
      const QHash<unsigned int, XtalCompositionStruct> & limits,
      int maxAttempts, GlobalSearch::Atom **atom)
  {
    Vector3 cartCoords;
    bool success;

    // For first atom, add to 0, 0, 0
    if (numAtoms() == 0) {
      cartCoords = Vector3 (0,0,0);
    }
    else {
      unsigned int i = 0;
      vector3 fracCoords;

      // Cache the minimum radius for the new atom
      const double newMinRadius = limits.value(atomicNumber).minRadius;

      // Compute a cut off distance -- atoms farther away than this value
      // will abort the check early.
      double maxCheckDistance = 0.0;
      for (QHash<unsigned int, XtalCompositionStruct>::const_iterator
           it = limits.constBegin(), it_end = limits.constEnd(); it != it_end;
           ++it) {
        if (it.value().minRadius > maxCheckDistance) {
          maxCheckDistance = it.value().minRadius;
        }
      }
      maxCheckDistance += newMinRadius;
      const double maxCheckDistSquared = maxCheckDistance*maxCheckDistance;

      do {
        // Reset sentinal
        success = true;

        // Generate fractional coordinates
        fracCoords.Set(RANDDOUBLE(), RANDDOUBLE(), RANDDOUBLE());

        // Convert to cartesian coordinates and store
        cartCoords = Vector3(this->fracToCart(fracCoords).AsArray());

        // Compare distance to each atom in xtal with minimum radii
        QVector<double> squaredDists;
        this->getSquaredAtomicDistancesToPoint(cartCoords, &squaredDists);
        Q_ASSERT_X(squaredDists.size() == this->numAtoms(), Q_FUNC_INFO,
                   "Size of distance list does not match number of atoms.");

        for (int dist_ind = 0; dist_ind < squaredDists.size(); ++dist_ind) {
          const double &curDistSquared = squaredDists[dist_ind];
          // Save a bit of time if distance is huge...
          if (curDistSquared > maxCheckDistSquared) {
            continue;
          }
          // Compare distance to minimum:
          const double minDist = newMinRadius + limits.value(
                this->atom(dist_ind).atomicNumber()).minRadius;
          const double minDistSquared = minDist * minDist;

          if (curDistSquared < minDistSquared) {
            success = false;
            break;
          }
        }

      } while (++i < maxAttempts && !success);

      if (i >= maxAttempts) return false;
    }
    Atom *atm = addAtom();
    atom = &atm;
    (*atom)->setPos(cartCoords);
    (*atom)->setAtomicNumber(static_cast<int>(atomicNumber));
    return true;
  }

  //MolUnit corrected function
  bool Xtal::addAtomRandomly(
      unsigned int atomicNumber,
      unsigned int neighbor,
      const QHash<unsigned int, XtalCompositionStruct> & limits,
      const QHash<QPair<int, int>, MolUnit> & limitsMolUnit,
      bool useMolUnit,
      int maxAttempts, GlobalSearch::Atom **atom)
  {
    Vector3 cartCoords;
    bool success;

    // For first atom, add to 0, 0, 0
    if (numAtoms() == 0) {
      cartCoords = Vector3 (0,0,0);
    }
    else {
      unsigned int i = 0;
      vector3 fracCoords;

      // Cache the minimum radius for the new atom
      const double newMinRadius = limits.value(atomicNumber).minRadius;

      // Compute a cut off distance -- atoms farther away than this value
      // will abort the check early.
      double maxCheckDistance = 0.0;
      if (atomicNumber == 0)
          maxCheckDistance = 1;
      else {
        for (QHash<unsigned int, XtalCompositionStruct>::const_iterator
             it = limits.constBegin(), it_end = limits.constEnd(); it != it_end;
             ++it) {
          if (it.value().minRadius > maxCheckDistance) {
            maxCheckDistance = it.value().minRadius;
          }
        }
      }
      if (atomicNumber != 0)
        maxCheckDistance += newMinRadius;
      const double maxCheckDistSquared = maxCheckDistance*maxCheckDistance;

      do {
        // Reset sentinal
        success = true;

        // Generate fractional coordinates
        fracCoords.Set(RANDDOUBLE(), RANDDOUBLE(), RANDDOUBLE());

        // Convert to cartesian coordinates and store
        cartCoords = Vector3(this->fracToCart(fracCoords).AsArray());

        // Compare distance to each atom in xtal with minimum radii
        QVector<double> squaredDists;
        this->getSquaredAtomicDistancesToPoint(cartCoords, &squaredDists);
        Q_ASSERT_X(squaredDists.size() == this->numAtoms(), Q_FUNC_INFO,
                   "Size of distance list does not match number of atoms.");

        for (int dist_ind = 0; dist_ind < squaredDists.size(); ++dist_ind) {
          const double &curDistSquared = squaredDists[dist_ind];
          // Save a bit of time if distance is huge...
          if (curDistSquared > maxCheckDistSquared) {
            continue;
          }
          // Compare distance to minimum:
          const double minDist = newMinRadius + limits.value(
                this->atom(dist_ind).atomicNumber()).minRadius;
          const double minDistSquared = minDist * minDist;

          if (curDistSquared < minDistSquared) {
            success = false;
            break;
          }
        }

      } while (++i < maxAttempts && !success);

      if (i >= maxAttempts) return false;
    }
    Atom *atm = addAtom();
    atom = &atm;
    (*atom)->setPos(cartCoords);
    (*atom)->setAtomicNumber(static_cast<int>(atomicNumber));

    if (useMolUnit == true) {
      int numNeighbors = 0;
      double dist = 0.0;
      int geom = 0;

      for (QHash<QPair<int, int>, MolUnit>::const_iterator it = limitsMolUnit.constBegin(), it_end = limitsMolUnit.constEnd(); it != it_end; it++) {
        QPair<int, int> key = const_cast<QPair<int, int> &>(it.key());
        if (atomicNumber == key.first && neighbor == key.second) {
          numNeighbors = it.value().numNeighbors;
          geom = it.value().geom;
          dist = it.value().dist;
        }
      }

      OpenBabel::OBMol obmol = OBMol();
      OpenBabel::OBAtom *obatom = obmol.GetAtom((*atom)->index()+1);
      obatom->SetAtomicNum(6);
      obatom->SetImplicitValence(numNeighbors);
      obmol.SetImplicitValencePerceived();
      obatom->SetHyb(geom);
      obmol.SetHybridizationPerceived();
      obmol.AddHydrogens(obatom);
      unsigned int numberAtoms = numAtoms();

      int j = 0;
      for (unsigned int i = numberAtoms+1; i <= obmol.NumAtoms(); ++i, ++j) {
        OpenBabel::OBAtom *obatom2 = obmol.GetAtom(i);
        double currDist = obatom2->GetDistance(obatom);
        vector3 v = ((obatom2->GetVector()-obatom->GetVector())*(dist/currDist))+obatom->GetVector();
        obatom2->SetVector(v);
        Atom *a;
        a = addAtom();
        a->setOBAtom(obatom2);
        a->setAtomicNumber(neighbor);
      }
      if (atomicNumber == 0)
          removeAtom(*atom);
    }

    return true;
  }

  bool Xtal::fillSuperCell(int a, int b, int c, Xtal * myXtal) {
      //qDebug() << "Xtal has a=" << a << " b=" << b << " c=" << c;

      QList<Atom*> oneFUatoms =  atoms();
      matrix3x3 obcellMatrix = myXtal->cell()->GetCellMatrix();
      vector3 obU1 = obcellMatrix.GetRow(0);
      vector3 obU2 = obcellMatrix.GetRow(1);
      vector3 obU3 = obcellMatrix.GetRow(2);
      // Scale cell
      double A = myXtal->getA();
      double B = myXtal->getB();
      double C = myXtal->getC();
      myXtal->setCellInfo(a * A,
                  b * B,
                  c * C,
                  myXtal->getAlpha(),
                  myXtal->getBeta(),
                  myXtal->getGamma());
      //qDebug() << "Xtal cell dimensions are increasing from a=" << A << "b=" << B << "c=" << C <<
      //            "to a=" << a*A << "b=" << b*B << "c=" << c*C;
      a--;
      b--;
      c--;

      for (int i = 0; i <= a; i++) {
          for (int j = 0; j <= b; j++) {
              for (int k = 0; k <= c; k++) {
                  if (i == 0 && j == 0 && k == 0) continue;
                  Vector3 uVecs(
                          obU1.x() * i + obU2.x() * j + obU3.x() * k,
                          obU1.y() * i + obU2.y() * j + obU3.y() * k,
                          obU1.z() * i + obU2.z() * j + obU3.z() * k);
                  //Vector3 uVecs(this->getA() * i, this->getB() * j, this-> getC() * k);
                  foreach(Atom *atom, oneFUatoms) {
                      Atom *newAtom = myXtal->addAtom();
                      *newAtom = *atom;
                      newAtom->setPos((*atom.pos())+uVecs);
                      newAtom->setAtomicNumber(atom.atomicNumber());
                      //qDebug() << "Added atom at a=" << i << " b=" << j << " c=" << k << " with atomic number " << newAtom.atomicNumber();
                  }
              }
          }
      }
      return true;
  }

  bool Xtal::checkInteratomicDistances(
      const QHash<unsigned int, XtalCompositionStruct> &limits,
      int *atom1, int *atom2, double *IAD)
  {
      // Compute a cut off distance -- atoms farther away than this value
      // will abort the check early.
      double maxCheckDistance = 0.0;
      for (QHash<unsigned int, XtalCompositionStruct>::const_iterator
           it = limits.constBegin(), it_end = limits.constEnd(); it != it_end;
           ++it) {
        if (it.value().minRadius > maxCheckDistance) {
          maxCheckDistance = it.value().minRadius;
        }
      }
      maxCheckDistance += maxCheckDistance;
      const double maxCheckDistSquared = maxCheckDistance*maxCheckDistance;

      // Iterate through all of the atoms in the molecule for "a1"
      for (QList<Atom*>::const_iterator a1 = m_atomList.constBegin(),
           a1_end = m_atomList.constEnd(); a1 != a1_end; ++a1) {

        // Get list of minimum squared distances between each atom and a1
        QVector<double> squaredDists;
        this->getSquaredAtomicDistancesToPoint(*(*a1).pos(), &squaredDists);
        Q_ASSERT_X(squaredDists.size() == this->numAtoms(), Q_FUNC_INFO,
                   "Size of distance list does not match number of atoms.");

        // Cache the minimum radius of a1
        const double minA1Radius =
            limits.value((*a1).atomicNumber()).minRadius;

        // Iterate through each distance
        for (int i = 0; i < squaredDists.size(); ++i) {

          // Grab the atom pointer at i, a2
          Atom *a2 = this->atom(i);

          // If a1 and a2 are the same, skip the comparison
          if (*a1 == a2) {
            continue;
          }

          // Cache the squared distance between a1 and a2
          const double &curDistSquared = squaredDists[i];

          // Skip comparison if the current distance exceeds the cutoff
          if (curDistSquared > maxCheckDistSquared) {
            continue;
          }

          // Calculate the minimum distance for the atom pair
          const double minDist = limits.value(a2.atomicNumber()).minRadius
              + minA1Radius;
          const double minDistSquared = minDist * minDist;

          // If the distance is too small, set atom1/atom2 and return false
          if (curDistSquared < minDistSquared) {
            if (atom1 != NULL && atom2 != NULL) {
              *atom1 = m_atomList.indexOf(*a1);
              *atom2 = m_atomList.indexOf(a2);
              if (IAD != NULL) {
                *IAD = sqrt(curDistSquared);
              }
            }
            return false;
          }

          // Atom a2 is ok with respect to a1
        }
        // Atom a1 is ok will all a2
      }
      // all distances check out -- return true.
      if (atom1 != NULL && atom2 != NULL) {
        *atom1 = *atom2 = -1;
        if (IAD != NULL) {
          *IAD = 0.0;
        }
      }
      return true;
  }

  bool Xtal::getShortestInteratomicDistance(double & shortest) const {
    QList<Atom*> atomList = atoms();
    if (atomList.size() <= 1) return false; // Need at least two atoms!
    QList<Vector3> atomPositions;
    for (int i = 0; i < atomList.size(); i++)
      atomPositions.push_back(*(atomList.at(i).pos()));

    // Initialize vars
    //  Atomic Positions
    Vector3 v1= atomPositions.at(0);
    Vector3 v2= atomPositions.at(1);
    //  Unit Cell Vectors
    //  First get OB matrix, extract vectors, then convert to Vector3's
    matrix3x3 obcellMatrix = cell()->GetCellMatrix();
    vector3 obU1 = obcellMatrix.GetRow(0);
    vector3 obU2 = obcellMatrix.GetRow(1);
    vector3 obU3 = obcellMatrix.GetRow(2);
    Vector3 u1 (obU1.x(), obU1.y(), obU1.z());
    Vector3 u2 (obU2.x(), obU2.y(), obU2.z());
    Vector3 u3 (obU3.x(), obU3.y(), obU3.z());
    //  Find all combinations of unit cell vectors to get wrapped neighbors
    QList<Vector3> uVecs;
    int s_1, s_2, s_3; // will be -1, 0, +1 multipliers
    for (s_1 = -1; s_1 <= 1; s_1++) {
      for (s_2 = -1; s_2 <= 1; s_2++) {
        for (s_3 = -1; s_3 <= 1; s_3++) {
          uVecs.append(s_1*u1 + s_2*u2 + s_3*u3);
        }
      }
    }

    shortest = fabs((v1-v2).norm());
    double distance;

    // Find shortest distance
    for (int i = 0; i < atomList.size(); i++) {
      v1 = atomPositions.at(i);
      for (int j = i+1; j < atomList.size(); j++) {
        v2 = atomPositions.at(j);
        // Intercell
        distance = fabs((v1-v2).norm());
        if (distance < shortest) shortest = distance;
        // Intracell
        for (int vecInd = 0; vecInd < uVecs.size(); vecInd++) {
          distance = fabs(((v1+uVecs.at(vecInd))-v2).norm());
          if (distance < shortest) shortest = distance;
        }
      }
    }

    return true;
  }

  bool Xtal::getSquaredAtomicDistancesToPoint(const Vector3 &coord,
                                              QVector<double> *distances)
  {
    int atmCount = this->numAtoms();
    if (atmCount < 1) {
      return false;
    }

    // Allocate memory
    distances->resize(atmCount);

    // Create list of all translation vectors to build a 3x3x3 supercell
    //  First get OB matrix, extract vectors, then convert to Vector3's
    matrix3x3 obcellMatrix = cell()->GetCellMatrix();
    const Vector3 u1 (obcellMatrix.GetRow(0).AsArray());
    const Vector3 u2 (obcellMatrix.GetRow(1).AsArray());
    const Vector3 u3 (obcellMatrix.GetRow(2).AsArray());
    //  Find all combinations of unit cell vectors to get wrapped neighbors
    QVector<Vector3> uVecs;
    uVecs.clear();
    uVecs.reserve(27);
    short s_1, s_2, s_3; // will be -1, 0, +1 multipliers
    for (s_1 = -1; s_1 <= 1; ++s_1) {
      for (s_2 = -1; s_2 <= 1; ++s_2) {
        for (s_3 = -1; s_3 <= 1; ++s_3) {
          uVecs.append(s_1*u1 + s_2*u2 + s_3*u3);
        }
      }
    }

    for (int i = 0; i < atmCount; ++i) {
      const Vector3 *pos = (this->atom(i).pos());
      double shortest = DBL_MAX;
      for (QVector<Vector3>::const_iterator it = uVecs.constBegin(),
           it_end = uVecs.constEnd(); it != it_end; ++it) {
        register double current = ((*it + *pos) - coord).squaredNorm();
        if (current < shortest) {
          shortest = current;
        }
      }
      (*distances)[i] = shortest;
    }

    return true;
  }


  bool Xtal::getNearestNeighborDistance(const double x,
                                        const double y,
                                        const double z,
                                        double & shortest) const {
    if (this->numAtoms() < 1) {
      return false; // Need at least one atom!
    }

    // Initialize vars
    //  Atomic Positions
    Vector3 v1 (x, y, z);
    const Vector3 *v2 = this->atom(0).pos();
    //  Unit Cell Vectors
    //  First get OB matrix, extract vectors, then convert to Vector3's
    matrix3x3 obcellMatrix = cell()->GetCellMatrix();
    vector3 obU1 = obcellMatrix.GetRow(0);
    vector3 obU2 = obcellMatrix.GetRow(1);
    vector3 obU3 = obcellMatrix.GetRow(2);
    Vector3 u1 (obU1.x(), obU1.y(), obU1.z());
    Vector3 u2 (obU2.x(), obU2.y(), obU2.z());
    Vector3 u3 (obU3.x(), obU3.y(), obU3.z());
    //  Find all combinations of unit cell vectors to get wrapped neighbors
    QList<Vector3> uVecs;
    int s_1, s_2, s_3; // will be -1, 0, +1 multipliers
    for (s_1 = -1; s_1 <= 1; s_1++) {
      for (s_2 = -1; s_2 <= 1; s_2++) {
        for (s_3 = -1; s_3 <= 1; s_3++) {
          uVecs.append(s_1*u1 + s_2*u2 + s_3*u3);
        }
      }
    }

    shortest = fabs((v1 - (*v2) ).norm());

    double distance;

    // Find shortest distance
    for (int j = 0; j < this->numAtoms(); j++) {
      v2 = this->atom(j).pos();
      // Intercell
      distance = fabs((v1 - (*v2)).norm());
      if (distance < shortest) shortest = distance;
      // Intracell
      for (int vecInd = 0; vecInd < uVecs.size(); vecInd++) {
        distance = fabs((((*v2)+uVecs.at(vecInd)) - v1).norm());
        if (distance < shortest) shortest = distance;
      }
    }
    return true;
  }

  bool Xtal::getIADHistogram(QList<double> * distance,
                             QList<double> * frequency,
                             double min, double max, double step,
                             Atom *atom) const {

    if (min > max && step > 0) {
      qWarning() << "Xtal::getIADHistogram: min cannot be greater than max!";
      return false;
    }
    if (step < 0 || step == 0) {
      qWarning() << "Xtal::getIADHistogram: invalid step size:" << step;
      return false;
    }

    // Populate distance list
    distance->clear();
    frequency->clear();
    double val = min;
    do {
      distance->append(val);
      frequency->append(0);
      val += step;
    } while (val < max);

    QList<Atom*> atomList = atoms();
    QList<Vector3> atomPositions;
    for (int i = 0; i < atomList.size(); i++)
      atomPositions.push_back(*(atomList.at(i).pos()));

    // Initialize vars
    //  Atomic Positions
    Vector3 v1= atomPositions.at(0);
    Vector3 v2= atomPositions.at(1);
    //  Unit Cell Vectors
    //  First get OB matrix, extract vectors, then convert to Vector3's
    matrix3x3 obcellMatrix = cell()->GetCellMatrix();
    vector3 obU1 = obcellMatrix.GetRow(0);
    vector3 obU2 = obcellMatrix.GetRow(1);
    vector3 obU3 = obcellMatrix.GetRow(2);
    Vector3 u1 (obU1.x(), obU1.y(), obU1.z());
    Vector3 u2 (obU2.x(), obU2.y(), obU2.z());
    Vector3 u3 (obU3.x(), obU3.y(), obU3.z());
    //  Find all combinations of unit cell vectors to get wrapped neighbors
    QList<Vector3> uVecs;
    int s_1, s_2, s_3; // will be -1, 0, +1 multipliers
    for (s_1 = -1; s_1 <= 1; s_1++) {
      for (s_2 = -1; s_2 <= 1; s_2++) {
        for (s_3 = -1; s_3 <= 1; s_3++) {
          uVecs.append(s_1*u1 + s_2*u2 + s_3*u3);
        }
      }
    }
    double diff;

    // build histogram
    // Loop over all atoms
    if (atom == 0) {
      for (int i = 0; i < atomList.size(); i++) {
        v1 = atomPositions.at(i);
        for (int j = i+1; j < atomList.size(); j++) {
          v2 = atomPositions.at(j);
          // Intercell
          diff = fabs((v1-v2).norm());
          for (int k = 0; k < distance->size(); k++) {
            double radius = distance->at(k);
            if (fabs(diff-radius) < step/2) {
              (*frequency)[k]++;
            }
          }
          // Intracell
          for (int vecInd = 0; vecInd < uVecs.size(); vecInd++) {
            diff = fabs(((v1+uVecs.at(vecInd))-v2).norm());
            for (int k = 0; k < distance->size(); k++) {
              double radius = distance->at(k);
              if (fabs(diff-radius) < step/2) {
                (*frequency)[k]++;
              }
            }
          }
        }
      }
    }
    // Or, just the one requested
    else {
      v1 = *atom.pos();
      for (int j = 0; j < atomList.size(); j++) {
        v2 = atomPositions.at(j);
        // Intercell
        diff = fabs((v1-v2).norm());
        for (int k = 0; k < distance->size(); k++) {
          double radius = distance->at(k);
          if (diff != 0 && fabs(diff-radius) < step/2) {
            (*frequency)[k]++;
          }
        }
        // Intracell
        for (int vecInd = 0; vecInd < uVecs.size(); vecInd++) {
          diff = fabs(((v1+uVecs.at(vecInd))-v2).norm());
          for (int k = 0; k < distance->size(); k++) {
            double radius = distance->at(k);
            if (fabs(diff-radius) < step/2) {
              (*frequency)[k]++;
            }
          }
        }
      }
    }

    return true;
  }

  QList<Vector3> Xtal::getAtomCoordsFrac() const {
    QList<Vector3> list;
    // Sort by symbols
    QList<QString> symbols = getSymbols();
    QString symbol_ref;
    QString symbol_cur;
    QList<Atom*>::const_iterator it;
    for (int i = 0; i < symbols.size(); i++) {
      symbol_ref = symbols.at(i);
      for (it  = m_atomList.begin();
           it != m_atomList.end();
           it++) {
        symbol_cur = QString(OpenBabel::etab.GetSymbol((*it).atomicNumber()));
        if (symbol_cur == symbol_ref) {
          list.append( *(cartToFrac((*it).pos())));
        }
      }
    }
    return list;
  }

  Vector3 Xtal::fracToCart(const Vector3 & fracCoords) const {
    OpenBabel::vector3 obfrac (fracCoords.x(), fracCoords.y(), fracCoords.z());
    OpenBabel::vector3 obcart = cell()->FractionalToCartesian(obfrac);
    Vector3 cartCoords (obcart.x(), obcart.y(), obcart.z());
    return cartCoords;
  }

  Vector3 *Xtal::fracToCart(const Vector3 *fracCoords) const {
    OpenBabel::vector3 obfrac (fracCoords->x(), fracCoords->y(), fracCoords->z());
    OpenBabel::vector3 obcart = cell()->FractionalToCartesian(obfrac);
    Vector3 *cartCoords = new Vector3 (obcart.x(), obcart.y(), obcart.z());
    return cartCoords;
  }

  Vector3 Xtal::cartToFrac(const Vector3 & cartCoords) const {
    OpenBabel::vector3 obcart (cartCoords.x(), cartCoords.y(), cartCoords.z());
    OpenBabel::vector3 obfrac = cell()->CartesianToFractional(obcart);
    Vector3 fracCoords (obfrac.x(), obfrac.y(), obfrac.z());
    return fracCoords;
  }

  Vector3 *Xtal::cartToFrac(const Vector3 *cartCoords) const {
    OpenBabel::vector3 obcart (cartCoords->x(), cartCoords->y(), cartCoords->z());
    OpenBabel::vector3 obfrac = cell()->CartesianToFractional(obcart);
    Vector3 *fracCoords = new Vector3 (obfrac.x(), obfrac.y(), obfrac.z());
    return fracCoords;
  }

  void Xtal::wrapAtomsToCell() {
    //qDebug() << "Xtal::wrapAtomsToCell() called";
    // Store position of atoms in fractional units
    QList<Atom*> atomList       = atoms();
    QList<Vector3> fracCoordsList;
    for (int i = 0; i < atomList.size(); i++)
      fracCoordsList.append(cartToFrac(*(atomList.at(i).pos())));

    // wrap fractional coordinates to [0,1)
    for (int i = 0; i < fracCoordsList.size(); i++) {
      fracCoordsList[i][0] = fmod(fracCoordsList[i][0]+100, 1);
      fracCoordsList[i][1] = fmod(fracCoordsList[i][1]+100, 1);
      fracCoordsList[i][2] = fmod(fracCoordsList[i][2]+100, 1);
    }

    // Recalculate cartesian coordinates:
    Vector3 cartCoord;
    for (int i = 0; i < atomList.size(); i++) {
      cartCoord = fracToCart(fracCoordsList.at(i));
      atomList.at(i)->setPos(cartCoord);
    }
  }

  QHash<QString, QVariant> Xtal::getFingerprint()
  {
    QHash<QString, QVariant> fp = Structure::getFingerprint();
    fp.insert("volume", getVolume());
    fp.insert("spacegroup", getSpaceGroupNumber());
    return fp;
  }

  QString Xtal::getResultsEntry() const
  {
    QString status;
    switch (getStatus()) {
    case Optimized:
      status = "Optimized";
      break;
    case Killed:
    case Removed:
      status = "Killed";
      break;
    case Duplicate:
      status = "Duplicate";
      break;
    case Supercell:
      status = "Supercell";
      break;
    case Error:
      status = "Error";
      break;
    case StepOptimized:
    case WaitingForOptimization:
    case InProcess:
    case Empty:
    case Updating:
    case Submitted:
    default:
      status = "In progress";
      break;
    }
    return QString("%1 %2 %3 %4 %5 %6 %7")
      .arg(getRank(), 6)
      .arg(getGeneration(), 6)
      .arg(getIDNumber(), 6)
      .arg(getEnthalpy() / static_cast<double>(getFormulaUnits()), 13)
      .arg(getFormulaUnits(), 6)
      .arg(m_spgSymbol, 10)
      .arg(status, 11);
  };


  uint Xtal::getSpaceGroupNumber() {
    if (m_spgNumber > 230)
      findSpaceGroup();
    return m_spgNumber;
  }

  QString Xtal::getSpaceGroupSymbol() {
    if (m_spgNumber > 230)
      findSpaceGroup();
    return m_spgSymbol;
  }

  QString Xtal::getHTMLSpaceGroupSymbol() {
    if (m_spgNumber > 230)
      findSpaceGroup();

    QString s = m_spgSymbol;

    // Prepare HTML tags
    s.prepend("<HTML>");
    s.append("</HTML>");

    // "_X"  --> "<sub>X</sub>"
    int ind = s.indexOf("_");
    while (ind != -1) {
      s = s.insert(ind+2, "</sub>");
      s = s.replace(ind, 1, "<sub>");
      ind = s.indexOf("_");
    }

    // "-X"  --> "<span style="text-decoration: overline">X</span>"
    ind = s.indexOf("-");
    while (ind != -1) {
      s = s.insert(ind+2, "</span>");
      s = s.replace(ind, 1, "<span style=\"text-decoration: overline\">");
      ind = s.indexOf("-", ind+35);
    }

    return s;
  }

  void Xtal::findSpaceGroup(double prec) {
    // Check that the precision is reasonable
    if (prec < 1e-5) {
      qWarning() << "Xtal::findSpaceGroup called with a precision of "
                 << prec << ". This is likely an error. Resetting prec to "
                 << 0.05 << ".";
      prec = 0.05;
    }

    // reset space group to 0 so we can exit if needed
    m_spgNumber = 0;
    m_spgSymbol = "Unknown";
    int num = numAtoms();

    // if no unit cell or atoms, exit
    if (num == 0)
      return;
    else if (!cell()) {
      qWarning() << "Xtal::findSpaceGroup( " << prec << " ) called on an xtal with no cell!";
      return;
    }

    // get lattice matrix
    std::vector<OpenBabel::vector3> vecs = cell()->GetCellVectors();
    vector3 obU1 = vecs[0];
    vector3 obU2 = vecs[1];
    vector3 obU3 = vecs[2];
    double lattice[3][3] = {
      {obU1.x(), obU2.x(), obU3.x()},
      {obU1.y(), obU2.y(), obU3.y()},
      {obU1.z(), obU2.z(), obU3.z()}
    };

    // Get atom info
    double (*positions)[3] = new double[num][3];
    int *types = new int[num];
    QList<Atom*> atomList = atoms();
    Vector3 fracCoords;
    for (int i = 0; i < atomList.size(); i++) {
      fracCoords        = cartToFrac(*(atomList.at(i).pos()));
      types[i]          = atomList.at(i).atomicNumber();
      positions[i][0]   = fracCoords.x();
      positions[i][1]   = fracCoords.y();
      positions[i][2]   = fracCoords.z();
    }

    // find spacegroup
    char symbol[21];
    m_spgNumber = spg_get_international(symbol,
                                        lattice,
                                        positions,
                                        types,
                                        num, prec);

    delete [] positions;
    delete [] types;

    // Update the OBUnitCell object.
    cell()->SetSpaceGroup(m_spgNumber);

    // Fail if m_spgNumber is still 0
    if (m_spgNumber == 0) {
      return;
    }

    // Set and clean up the symbol string
    m_spgSymbol = QString(symbol);
    m_spgSymbol.remove(" ");
    return;
  }

  void Xtal::getSpglibFormat() const {
    std::vector<OpenBabel::vector3> vecs = cell()->GetCellVectors();
    vector3 obU1 = vecs[0];
    vector3 obU2 = vecs[1];
    vector3 obU3 = vecs[2];

    QString t;
    QTextStream out (&t);

    out << "double lattice[3][3] = {\n"
        << "  {" << obU1.x() << ", " << obU2.x() << ", " << obU3.x() << "},\n"
        << "  {" << obU1.y() << ", " << obU2.y() << ", " << obU3.y() << "},\n"
        << "  {" << obU1.z() << ", " << obU2.z() << ", " << obU3.z() << "}};\n\n";

    out << "double position[][3] = {";
    for (unsigned int i = 0; i < numAtoms(); i++) {
      if (i == 0 || i != numAtoms()-1) out << ",";
      out << "\n";
      out << "  {" << atom(i).pos()->x() << ", "
          << atom(i).pos()->y() << ", "
          << atom(i).pos()->z() << "}";
    }
    out << "};\n\n";
    out << "int types[] = { ";
    for (unsigned int i = 0; i < numAtoms(); i++) {
      if (i == 0 || i != numAtoms()-1) out << ", ";
      out << atom(i).atomicNumber();
    }
    out << " };\n";
    out << "int num_atom = " << numAtoms() << ";\n";
    qDebug() << t;
  }

  bool Xtal::rotateCellToStandardOrientation()
  {
    // Get correct matrix
    Matrix3 newMat
      (getCellMatrixInStandardOrientation());

    // check that the matrix is valid
    if (newMat.isZero()) {
      const OpenBabel::matrix3x3 mat = cell()->GetCellMatrix();
      qDebug() << "Cannot rotate cell to std orientation:\n"
               << QString("%L1 %L2 %L3\n%L4 %L5 %L6\n%L7 %L8 %L9")
        .arg(mat(0,0), -9, 'g').arg(mat(0,1), -9, 'g').arg(mat(0,2), -9, 'g')
        .arg(mat(1,0), -9, 'g').arg(mat(1,1), -9, 'g').arg(mat(1,2), -9, 'g')
        .arg(mat(2,0), -9, 'g').arg(mat(2,1), -9, 'g').arg(mat(2,2), -9, 'g');
      return false;
    }

    // Set the rotated basis
    setCellInfo(Eigen2OB(newMat));

    return true;
  }

  bool Xtal::rotateCellAndCoordsToStandardOrientation()
  {
    // Cache fractional coordinates
    QList<Vector3> fcoords;
    for (QList<Atom*>::const_iterator it = m_atomList.constBegin(),
           it_end = m_atomList.constEnd(); it != it_end; ++it) {
      fcoords.append( cartToFrac(*(*it).pos()));
    }

    if (!rotateCellToStandardOrientation()) {
      return false;
    }

    // Reset coords
    Q_ASSERT(this->m_atomList.size() == fcoords.size());
    for (int i = 0; i < m_atomList.size(); ++i) {
      this->atom(i)->setPos(this->fracToCart(fcoords[i]));
    }

    return true;
  }

  Matrix3 Xtal::getCellMatrixInStandardOrientation() const
  {
    // Cell matrix as row vectors
    const OpenBabel::matrix3x3 origRowMat = cell()->GetCellMatrix();
    return getCellMatrixInStandardOrientation(OB2Eigen(origRowMat));
  }

  Matrix3 Xtal::getCellMatrixInStandardOrientation
  (const Matrix3 &origRowMat)
  {
    // Extract vector components:
    const double &x1 = origRowMat(0,0);
    const double &y1 = origRowMat(0,1);
    const double &z1 = origRowMat(0,2);

    const double &x2 = origRowMat(1,0);
    const double &y2 = origRowMat(1,1);
    const double &z2 = origRowMat(1,2);

    const double &x3 = origRowMat(2,0);
    const double &y3 = origRowMat(2,1);
    const double &z3 = origRowMat(2,2);

    // Cache some frequently used values:
    // Length of v1
    const double L1 = sqrt(x1*x1 + y1*y1 + z1*z1);
    // Squared norm of v1's yz projection
    const double sqrdnorm1yz = y1*y1 + z1*z1;
    // Squared norm of v2's yz projection
    const double sqrdnorm2yz = y2*y2 + z2*z2;
    // Determinant of v1 and v2's projections in yz plane
    const double detv1v2yz = y2*z1 - y1*z2;
    // Scalar product of v1 and v2's projections in yz plane
    const double dotv1v2yz = y1*y2 + z1*z2;

    // Used for denominators, since we want to check that they are
    // sufficiently far from 0 to keep things reasonable:
    double denom;
    const double DENOM_TOL = 1e-5;

    // Create target matrix, fill with zeros
    Matrix3 newMat (Matrix3::Zero());

    // Set components of new v1:
    newMat(0,0) = L1;

    // Set components of new v2:
    denom = L1;
    if (fabs(denom) < DENOM_TOL) {
      return Matrix3::Zero();
    };
    newMat(1,0) = (x1*x2 + y1*y2 + z1*z2) / denom;

    newMat(1,1) = sqrt(x2*x2 * sqrdnorm1yz +
                       detv1v2yz*detv1v2yz -
                       2*x1*x2*dotv1v2yz +
                       x1*x1*sqrdnorm2yz) / denom;

    // Set components of new v3
    // denom is still L1
    Q_ASSERT(denom == L1);
    newMat(2,0) = (x1*x3 + y1*y3 + z1*z3) / denom;

    denom = L1*L1 * newMat(1,1);
    if (fabs(denom) < DENOM_TOL) {
      return Matrix3::Zero();
    };
    newMat(2,1) = (x1*x1*(y2*y3 + z2*z3) +
                   x2*(x3*sqrdnorm1yz -
                       x1*(y1*y3 + z1*z3)
                       ) +
                   detv1v2yz*(y3*z1 - y1*z3) -
                   x1*x3*dotv1v2yz) / denom;

    denom = L1 * newMat(1,1);
    if (fabs(denom) < DENOM_TOL) {
      return Matrix3::Zero();
    };
    // Numerator is determinant of original cell:
    newMat(2,2) = (x1*y2*z3 - x1*y3*z2 +
                   x2*y3*z1 - x2*y1*z3 +
                   x3*y1*z2 - x3*y2*z1) / denom;

    return newMat;
  }

  // Initialize static members for COB list generation
  QMutex Xtal::m_validCOBsGenMutex;
  QVector<Matrix3> Xtal::m_transformationMatrices;
  QVector<Matrix3> Xtal::m_mixMatrices;

  static inline bool COBIsValid(const Matrix3 &cob)
  {
    // determinant must be +/- 1
    if (fabs(fabs(cob.determinant()) - 1.0) < 1e-4)
      return false;

    return true;
  }

  void Xtal::generateValidCOBs()
  {
    m_validCOBsGenMutex.lock();

    // Has another instance beat us to the punch?
    if (m_mixMatrices.size()) {
      m_validCOBsGenMutex.unlock();
      return;
    }

    Matrix3 tmpMat;

    m_transformationMatrices.clear();
    m_transformationMatrices.reserve(32);
    m_mixMatrices.clear();
    m_mixMatrices.reserve(8);

    // Build list of transformation matrices
    // First build list of 90 degree rotations
    tmpMat <<  1, 0, 0,   0, 1, 0,   0, 0, 1; m_transformationMatrices<<tmpMat;
    tmpMat <<  1, 0, 0,   0, 0, 1,   0, 1, 0; m_transformationMatrices<<tmpMat;
    tmpMat <<  0, 1, 0,   1, 0, 0,   0, 0, 1; m_transformationMatrices<<tmpMat;
    tmpMat <<  0, 0, 1,   0, 1, 0,   1, 0, 0; m_transformationMatrices<<tmpMat;
    for (unsigned short int i = 0; i < 4; ++i) {
      // Now apply all possible reflections to 90 rotations
      tmpMat <<-1, 0, 0,   0, 1, 0,   0, 0, 1;
      m_transformationMatrices << (tmpMat * m_transformationMatrices[i]);
      tmpMat << 1, 0, 0,   0,-1, 0,   0, 0, 1;
      m_transformationMatrices << (tmpMat * m_transformationMatrices[i]);
      tmpMat << 1, 0, 0,   0, 1, 0,   0, 0,-1;
      m_transformationMatrices << (tmpMat * m_transformationMatrices[i]);
      tmpMat <<-1, 0, 0,   0,-1, 0,   0, 0, 1;
      m_transformationMatrices << (tmpMat * m_transformationMatrices[i]);
      tmpMat <<-1, 0, 0,   0, 1, 0,   0, 0,-1;
      m_transformationMatrices << (tmpMat * m_transformationMatrices[i]);
      tmpMat << 1, 0, 0,   0,-1, 0,   0, 0,-1;
      m_transformationMatrices << (tmpMat * m_transformationMatrices[i]);
      tmpMat <<-1, 0, 0,   0,-1, 0,   0, 0,-1;
      m_transformationMatrices << (tmpMat * m_transformationMatrices[i]);
    }

    // Now build list of mix matrices
    // Identity
    tmpMat <<  1, 0, 0,   0, 1, 0,   0, 0, 1; m_mixMatrices.append(tmpMat);
    // Upper triangular mixes
    tmpMat <<  1, 1, 0,   0, 1, 0,   0, 0, 1; m_mixMatrices.append(tmpMat);
    tmpMat <<  1, 1, 1,   0, 1, 0,   0, 0, 1; m_mixMatrices.append(tmpMat);
    tmpMat <<  1, 1, 0,   0, 1, 1,   0, 0, 1; m_mixMatrices.append(tmpMat);
    tmpMat <<  1, 1, 1,   0, 1, 1,   0, 0, 1; m_mixMatrices.append(tmpMat);
    tmpMat <<  1, 0, 1,   0, 1, 0,   0, 0, 1; m_mixMatrices.append(tmpMat);
    tmpMat <<  1, 0, 1,   0, 1, 1,   0, 0, 1; m_mixMatrices.append(tmpMat);
    tmpMat <<  1, 0, 0,   0, 1, 1,   0, 0, 1; m_mixMatrices.append(tmpMat);

    m_validCOBsGenMutex.unlock();
  }

  Xtal * Xtal::getRandomRepresentation() const
  {
    // Cache volume for later sanity checks
    const double origVolume = cell()->GetCellVolume();

    // Randomly select a mix matrix to create a new cell matrix by
    // taking a linear combination of the current cell vectors
    const Matrix3 &mix
      (m_mixMatrices[RANDUINT() % m_mixMatrices.size()]);

    // Build new Xtal with the new basis
    Xtal *nxtal = new Xtal (this->parent());
    nxtal->setCellInfo(Eigen2OB(mix) * this->cell()->GetCellMatrix());

    Q_ASSERT_X(StableComp::eq(origVolume, nxtal->cell()->GetCellVolume()),
               Q_FUNC_INFO, "Randomized cell volume not "
               "equal to original structure.");

    // Generate a random translation (i.e. between 0 and 1)
    const double maxTranslation = getA() + getB() + getC();
    const Vector3 randTranslation
      (RANDDOUBLE() * maxTranslation,
       RANDDOUBLE() * maxTranslation,
       RANDDOUBLE() * maxTranslation);

    // Add atoms
    for (QList<Atom*>::const_iterator it = m_atomList.constBegin(),
           it_end = m_atomList.constEnd(); it != it_end; ++it) {
      Atom * atom = nxtal->addAtom();
      atom->setAtomicNumber((*it).atomicNumber());
      atom->setPos( (*(*it).pos()) + randTranslation);
    }

    // rotate and wrap:
    nxtal->rotateCellAndCoordsToStandardOrientation();
    nxtal->wrapAtomsToCell();
    return nxtal;
  }

  Xtal* Xtal::POSCARToXtal(const QString &poscar)
  {
    QTextStream ps (&const_cast<QString &>(poscar));
    QStringList sl;
    vector3 v1, v2, v3, pos;
    Xtal *xtal = new Xtal;

    ps.readLine(); // title
    float scale = ps.readLine().toFloat(); // Scale factor
    sl = ps.readLine().split(QRegExp("\\s+"), QString::SkipEmptyParts); // v1
    v1.x() = sl.at(0).toFloat() * scale;
    v1.y() = sl.at(1).toFloat() * scale;
    v1.z() = sl.at(2).toFloat() * scale;

    sl = ps.readLine().split(QRegExp("\\s+"), QString::SkipEmptyParts); // v2
    v2.x() = sl.at(0).toFloat() * scale;
    v2.y() = sl.at(1).toFloat() * scale;
    v2.z() = sl.at(2).toFloat() * scale;

    sl = ps.readLine().split(QRegExp("\\s+"), QString::SkipEmptyParts); // v3
    v3.x() = sl.at(0).toFloat() * scale;
    v3.y() = sl.at(1).toFloat() * scale;
    v3.z() = sl.at(2).toFloat() * scale;

    xtal->setCellInfo(v1, v2, v3);

    sl = ps.readLine().split(QRegExp("\\s+"), QString::SkipEmptyParts); // atom types
    unsigned int numAtomTypes = sl.size();
    QList<unsigned int> atomCounts;
    for (int i = 0; i < numAtomTypes; i++) {
      atomCounts.append(sl.at(i).toUInt());
    }

    // TODO this will assume fractional coordinates. VASP can use cartesian!
    ps.readLine(); // direct or cartesian
    // Atom coords begin
    Atom *atom;
    for (unsigned int i = 0; i < numAtomTypes; i++) {
      for (unsigned int j = 0; j < atomCounts.at(i); j++) {
        // Actual identity of the atoms doesn't matter for the symmetry
        // test. Just use (i+1) as the atomic number.
        atom = xtal->addAtom();
        atom->setAtomicNumber(i+1);
        // Get coords
        sl = ps.readLine().split(QRegExp("\\s+"), QString::SkipEmptyParts); // coords
        Vector3 pos;
        pos.x() = sl.at(0).toDouble();
        pos.y() = sl.at(1).toDouble();
        pos.z() = sl.at(2).toDouble();
        atom->setPos(xtal->fracToCart(pos));
      }
    }

    return xtal;
  }

  Xtal* Xtal::POSCARToXtal(QFile *file)
  {
    QString poscar;
    file->open(QFile::ReadOnly);
    poscar = file->readAll();
    file->close();
    return POSCARToXtal(poscar);
  }
} // end namespace XtalOpt
