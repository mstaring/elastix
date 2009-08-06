/*=========================================================================

Program:   Insight Segmentation & Registration Toolkit
Module:    $RCSfile: itkKernelTransform2.h,v $
Language:  C++
Date:      $Date: 2006-11-28 14:22:18 $
Version:   $Revision: 1.1 $

Copyright (c) Insight Software Consortium. All rights reserved.
See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

This software is distributed WITHOUT ANY WARRANTY; without even 
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __itkKernelTransform2_h
#define __itkKernelTransform2_h

#include "itkAdvancedTransform.h"
#include "itkPoint.h"
#include "itkVector.h"
#include "itkMatrix.h"
#include "itkPointSet.h"
#include <deque>
#include <math.h>
#include "vnl/vnl_matrix_fixed.h"
#include "vnl/vnl_matrix.h"
#include "vnl/vnl_vector.h"
#include "vnl/vnl_vector_fixed.h"
#include "vnl/algo/vnl_svd.h"
#include "vnl/vnl_sample.h"

namespace itk
{

  /** \class KernelTransform2
  * Intended to be a base class for elastic body spline and thin plate spline.
  * This is implemented in as straightforward a manner as possible from the
  * IEEE TMI paper by Davis, Khotanzad, Flamig, and Harms, Vol. 16,
  * No. 3 June 1997. Notation closely follows their paper, so if you have it
  * in front of you, this code will make a lot more sense.
  *
  * KernelTransform2:
  *  Provides support for defining source and target landmarks
  *  Defines a number of data types used in the computations
  *  Defines the mathematical framework used to compute all splines,
  *    so that subclasses need only provide a kernel specific to
  *    that spline
  *
  * This formulation allows the stiffness of the spline to
  * be adjusted, allowing the spline to vary from interpolating the
  * landmarks to approximating the landmarks.  This part of the
  * formulation is based on the short paper by R. Sprengel, K. Rohr,
  * H. Stiehl. "Thin-Plate Spline Approximation for Image
  * Registration". In 18th International Conference of the IEEE
  * Engineering in Medicine and Biology Society. 1996.
  *
  * This class was modified to support its use in the ITK registration framework
  * by Rupert Brooks, McGill Centre for Intelligent Machines, Montreal, Canada
  * March 2007.  See the Insight Journal Paper  by Brooks, R., Arbel, T. 
  * "Improvements to the itk::KernelTransform and its subclasses."
  *
  * Modified a bit to include it in elastix:
  * - style
  * - make it inherit from AdvancedTransform
  * - make it threadsafe, like was done in the itk as well.
  *
  * \ingroup Transforms
  *
  */
  template <class TScalarType, // probably only float and double make sense here
    unsigned int NDimensions>   // Number of dimensions
  class KernelTransform2 : 
    public AdvancedTransform<TScalarType, NDimensions,NDimensions>
  {
  public:
    /** Standard class typedefs. */
    typedef KernelTransform2 Self;
    typedef AdvancedTransform<TScalarType, NDimensions, NDimensions >   Superclass;
    typedef SmartPointer<Self>        Pointer;
    typedef SmartPointer<const Self>  ConstPointer;

    /** Run-time type information (and related methods). */
    itkTypeMacro( KernelTransform2, AdvancedTransform );

    /** New macro for creation of through a Smart Pointer */
    itkNewMacro( Self );

    /** Dimension of the domain space. */
    itkStaticConstMacro(SpaceDimension, unsigned int, NDimensions);

    /** Scalar type. */
    typedef typename Superclass::ScalarType  ScalarType;

    /** Parameters type. */
    typedef typename Superclass::ParametersType  ParametersType;

    /** Jacobian type. */
    typedef typename Superclass::JacobianType  JacobianType;

    /** Standard coordinate point type for this class. */
    typedef typename Superclass::InputPointType   InputPointType;
    typedef typename Superclass::OutputPointType  OutputPointType;

    /** Standard vector type for this class. */
    typedef typename Superclass::InputVectorType   InputVectorType;
    typedef typename Superclass::OutputVectorType  OutputVectorType;

    /** AdvancedTransform typedefs */
    typedef typename Superclass
      ::NonZeroJacobianIndicesType                    NonZeroJacobianIndicesType;
    typedef typename Superclass::SpatialJacobianType  SpatialJacobianType;
    typedef typename Superclass
      ::JacobianOfSpatialJacobianType                 JacobianOfSpatialJacobianType;
    typedef typename Superclass::SpatialHessianType   SpatialHessianType;
    typedef typename Superclass
      ::JacobianOfSpatialHessianType                  JacobianOfSpatialHessianType;
    typedef typename Superclass::InternalMatrixType   InternalMatrixType;

    /** PointList typedef. This type is used for maintaining lists of points,
    * specifically, the source and target landmark lists. */
    typedef DefaultStaticMeshTraits<TScalarType,
      NDimensions,
      NDimensions,
      TScalarType,
      TScalarType> PointSetTraitsType;
    typedef PointSet<InputPointType, NDimensions, PointSetTraitsType> PointSetType;
    typedef typename PointSetType::Pointer                        PointSetPointer;
    typedef typename PointSetType::PointsContainer                PointsContainer;
    typedef typename PointSetType::PointsContainerIterator        PointsIterator;
    typedef typename PointSetType::PointsContainerConstIterator   PointsConstIterator;

    /** VectorSet typedef. */
    typedef VectorContainer<unsigned long,InputVectorType> VectorSetType;
    typedef typename VectorSetType::Pointer        VectorSetPointer;

    /** 'I' (identity) matrix typedef. */
    typedef vnl_matrix_fixed<TScalarType, NDimensions, NDimensions> IMatrixType;

    /** Whether the advanced transform has nonzero matrices. */
    virtual bool GetHasNonZeroSpatialHessian( void ) const
    {
      return true;
    }
    virtual bool HasNonZeroJacobianOfSpatialHessian( void ) const
    {
      return true;
    }

    /** Return the number of parameters that completely define the Transfom  */
    virtual unsigned int GetNumberOfParameters(void) const 
    {
      return ( this->m_SourceLandmarks->GetNumberOfPoints() * SpaceDimension );
    }

    /** Get the source landmarks list, which we will denote \f$ p \f$. */
    itkGetObjectMacro( SourceLandmarks, PointSetType);

    /** Set the source landmarks list. */
    virtual void SetSourceLandmarks(PointSetType *);

    /** Get the target landmarks list, which we will denote  \f$ q \f$. */
    itkGetObjectMacro( TargetLandmarks, PointSetType);

    /** Set the target landmarks list. */
    virtual void SetTargetLandmarks(PointSetType *);

    /** Get the displacements list, which we will denote \f$ d \f$,
    * where \f$ d_i = q_i - p_i \f$. */
    itkGetObjectMacro( Displacements, VectorSetType );

    /** Compute W matrix. */
    void ComputeWMatrix(void);

    /** Compute L matrix inverse. */
    void ComputeLInverse();

    /** Compute the position of point in the new space */
    virtual OutputPointType TransformPoint(const InputPointType& thisPoint) const;

    /** Compute the Jacobian Matrix of the transformation at one point */
    virtual const JacobianType & GetJacobian(const InputPointType  &point ) const;

    /** Compute the Jacobian of the transformation. */
    virtual void GetJacobian(
      const InputPointType &,
      JacobianType &,
      NonZeroJacobianIndicesType & ) const;

    /** Set the Transformation Parameters to be an identity transform */
    virtual void SetIdentity();

    /** Set the Transformation Parameters and update the internal transformation. 
    * The parameters represent the source landmarks. Each landmark point is represented
    * by NDimensions doubles. All the landmarks are concatenated to form one flat
    * Array<double>. */
    virtual void SetParameters(const ParametersType &);

    /** Set Transform Fixed Parameters:
    *     To support the transform file writer this function was 
    *     added to set the target landmarks similar to the
    *     SetParameters function setting the source landmarks
    */
    virtual void SetFixedParameters(const ParametersType &);

    /** Update the Parameters array from the landmarks corrdinates. */
    virtual void UpdateParameters(void);

    /** Get the Transformation Parameters - Gets the Source Landmarks */
    virtual const ParametersType& GetParameters(void) const;

    /** Get Transform Fixed Parameters - Gets the Target Landmarks */
    virtual const ParametersType& GetFixedParameters(void) const;

    /** Stiffness of the spline.  A stiffness of zero results in the
    * standard interpolating spline.  A non-zero stiffness allows the
    * spline to approximate rather than interpolate the landmarks.
    * Stiffness values are usually rather small, typically in the range
    * of 0.001 to 0.1. The approximating spline formulation is based on
    * the short paper by R. Sprengel, K. Rohr, H. Stiehl. "Thin-Plate
    * Spline Approximation for Image Registration". In 18th
    * International Conference of the IEEE Engineering in Medicine and
    * Biology Society. 1996.
    */
    virtual void SetStiffness(double stiffness) 
    {m_Stiffness=(stiffness>0)?stiffness:0.0;
    m_LMatrixComputed=false;
    m_LInverseComputed=false;
    m_WMatrixComputed=false;
    }
    //itkSetClampMacro(Stiffness, double, 0.0, NumericTraits<double>::max());
    // Cant use the macro because the matrices must be recomputed
    itkGetMacro(Stiffness, double);

    /** This method makes only sense for the ElasticBody splines.
    * Declare here, so that you can always call it if you don't know
    * the type of kernel beforehand. It will be overridden in the 
    * ElasticBodySplineKernelTransform and in the 
    * ElasticBodyReciprocalSplineKernelTransform */
    virtual void SetAlpha(TScalarType Alpha) {};
    virtual TScalarType GetAlpha( void ) const 
    {
      // dummy value
      return -1.0;
    }

    /** This method makes only sense for the ElasticBody splines.
    * Declare here, so that you can always call it if you don't know
    * the type of kernel beforehand. It will be overridden in the 
    * ElasticBodySplineKernelTransform and in the 
    * ElasticBodyReciprocalSplineKernelTransform */
    virtual void SetPoissonRatio(TScalarType Nu) 
    {
      /** Just store it */
      this->m_PoissonRatio = Nu;
    };
    virtual TScalarType GetPoissonRatio(void) const 
    {
      /** return last set value */
      return this->m_PoissonRatio;
    }

  protected:
    KernelTransform2();
    virtual ~KernelTransform2();
    void PrintSelf(std::ostream& os, Indent indent) const;

  public:  
    /** 'G' matrix typedef. */
    typedef vnl_matrix_fixed<TScalarType, NDimensions, NDimensions> GMatrixType;

    /** 'L' matrix typedef. */
    typedef vnl_matrix<TScalarType> LMatrixType;

    /** 'K' matrix typedef. */
    typedef vnl_matrix<TScalarType> KMatrixType;

    /** 'P' matrix typedef. */
    typedef vnl_matrix<TScalarType> PMatrixType;

    /** 'Y' matrix typedef. */
    typedef vnl_matrix<TScalarType> YMatrixType;

    /** 'W' matrix typedef. */
    typedef vnl_matrix<TScalarType> WMatrixType;

    /** 'D' matrix typedef. Deformation component */
    typedef vnl_matrix<TScalarType> DMatrixType;

    /** 'A' matrix typedef. Rotational part of the Affine component */
    typedef vnl_matrix_fixed<TScalarType,NDimensions,NDimensions> AMatrixType;

    /** 'B' matrix typedef. Translational part of the Affine component */
    typedef vnl_vector_fixed<TScalarType,NDimensions> BMatrixType;

    /** Row matrix typedef. */
    typedef vnl_matrix_fixed<TScalarType, 1, NDimensions> RowMatrixType;

    /** Column matrix typedef. */
    typedef vnl_matrix_fixed<TScalarType, NDimensions, 1> ColumnMatrixType;

    /** The list of source landmarks, denoted 'p'. */
    PointSetPointer m_SourceLandmarks;

    /** The list of target landmarks, denoted 'q'. */
    PointSetPointer m_TargetLandmarks;

  protected:
    /** Compute G(x)
    * This is essentially the kernel of the transform.
    * By overriding this method, we can obtain (among others):
    *    Elastic body spline
    *    Thin plate spline
    *    Volume spline */
    virtual void ComputeG(const InputVectorType& landmarkVector, GMatrixType & GMatrix ) const;
    // depecrated:
    //virtual const GMatrixType & ComputeG(const InputVectorType & landmarkVector) const;

    /** Compute a G(x) for a point to itself (i.e. for the block
    * diagonal elements of the matrix K. Parameter indicates for which
    * landmark the reflexive G is to be computed. The default
    * implementation for the reflexive contribution is a diagonal
    * matrix where the diagonal elements are the stiffness of the
    * spline. */
    virtual void ComputeReflexiveG(PointsIterator, GMatrixType & GMatrix) const;
    //depecrate this one also in elastix:
    //virtual const GMatrixType & ComputeReflexiveG(PointsIterator) const;


    /** Compute the contribution of the landmarks weighted by the kernel funcion
    to the global deformation of the space  */
    virtual void ComputeDeformationContribution( const InputPointType & inputPoint,
      OutputPointType & result ) const;

    /** Compute K matrix. */
    void ComputeK();

    /** Compute L matrix. */
    void ComputeL();

    /** Compute P matrix. */
    void ComputeP();

    /** Compute Y matrix. */
    void ComputeY();

    /** Compute displacements \f$ q_i - p_i \f$. */
    void ComputeD();

    /** Reorganize the components of W into 
    D (deformable), A (rotation part of affine) 
    and B (translational part of affine ) components.
    \warning This method release the memory of the W Matrix  */
    void ReorganizeW(void);

    /** Stiffness parameter */
    double m_Stiffness;

    /** The list of displacements.
    * d[i] = q[i] - p[i]; */
    VectorSetPointer m_Displacements;

    /** The L matrix. */
    LMatrixType m_LMatrix;

    /** The inverse of L, which we also cache. */
    LMatrixType m_LMatrixInverse;

    /** The K matrix. */
    KMatrixType m_KMatrix;

    /** The P matrix. */
    PMatrixType m_PMatrix;

    /** The Y matrix. */
    YMatrixType m_YMatrix;

    /** The W matrix. */
    WMatrixType m_WMatrix;

    /** The Deformation matrix.
    This is an auxiliary matrix that will hold the
    Deformation (non-affine) part of the transform.
    Those are the coefficients that will multiply the
    Kernel function */
    DMatrixType m_DMatrix;

    /** Rotatinoal/Shearing part of the Affine component of the Transformation */
    AMatrixType m_AMatrix;

    /** Translational part of the Affine component of the Transformation */
    BMatrixType m_BVector;

    /** The G matrix. 
    It used to be mutable because m_GMatrix was made an ivar
    only to avoid copying the matrix at return time 
    but this is not necessary 
    SK: we don't need this matrix anymore as a member
    //GMatrixType m_GMatrix;

    /** Has the W matrix been computed? */
    bool m_WMatrixComputed;
    /** Has the L matrix been computed? */
    bool m_LMatrixComputed;
    /** Has the L inverse matrix been computed? */
    bool m_LInverseComputed;

    /** Identity matrix. */
    IMatrixType m_I;

    /** Precomputed nonzero Jacobian indices (simply all params) */
    NonZeroJacobianIndicesType m_NonZeroJacobianIndices;

    /** for old GetJacobian() method: */
    mutable NonZeroJacobianIndicesType m_NonZeroJacobianIndicesTemp;

  private:
    KernelTransform2(const Self&); //purposely not implemented
    void operator=(const Self&); //purposely not implemented

    TScalarType m_PoissonRatio;

  };

} // end namespace itk

#ifndef ITK_MANUAL_INSTANTIATION
#include "itkKernelTransform2.txx"
#endif

#endif // __itkKernelTransform2_h