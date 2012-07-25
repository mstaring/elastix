/*======================================================================

  This file is part of the elastix software.

  Copyright (c) University Medical Center Utrecht. All rights reserved.
  See src/CopyrightElastix.txt or http://elastix.isi.uu.nl/legal.php for
  details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE. See the above copyright notices for more information.

======================================================================*/

// GPU include files
#include "itkGPUCastImageFilter.h"
#include "itkGPUExplicitSynchronization.h"

#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkImageRegionConstIterator.h"

#include "itkTimeProbe.h"
#include "itkOpenCLUtil.h" // IsGPUAvailable()
#include <iomanip> // setprecision, etc.


//------------------------------------------------------------------------------
// This test compares the CPU with the GPU version of the GPUCastImageFilter.
// The filter takes an input image and produces an output image.
// We compare the CPU and GPU output image wrt RMSE and speed.

int main( int argc, char * argv[] )
{
  // Check arguments for help
  if( argc < 2 )
  {
    std::cerr << "ERROR: insufficient command line arguments.\n"
      << "  inputFileName outputDirectory" << std::endl;
    return EXIT_FAILURE;
  }

  // Check for GPU
  if( !itk::IsGPUAvailable() )
  {
    std::cerr << "ERROR: OpenCL-enabled GPU is not present." << std::endl;
    return EXIT_FAILURE;
  }

  /** Get the command line arguments. */
  std::string inputFileName = argv[1];
  std::string outputDirectory = argv[2];
  std::string baseName = inputFileName.substr( 0, inputFileName.rfind( "." ) );
  std::string outputFileNameCPU = outputDirectory + "/" + baseName + "-out-cpu.mha";
  std::string outputFileNameGPU = outputDirectory + "/" + baseName + "-out-gpu.mha";
  const double epsilon = 1e-3;
  const unsigned int runTimes = 5;

  std::cout << std::showpoint << std::setprecision( 4 );

  // Typedefs.
  const unsigned int  Dimension = 3;
  typedef short       InputPixelType;
  typedef float       OutputPixelType;
  typedef itk::Image<InputPixelType, Dimension>   InputImageType;
  typedef itk::Image<OutputPixelType, Dimension>  OutputImageType;

  // CPU Typedefs
  typedef itk::CastImageFilter<
    InputImageType, OutputImageType>              FilterType;
  typedef itk::ImageFileReader<InputImageType>    ReaderType;
  typedef itk::ImageFileWriter<OutputImageType>   WriterType;

  // Reader
  ReaderType::Pointer reader = ReaderType::New();
  reader->SetFileName( inputFileName );
  reader->Update();

  // Construct the filter
  FilterType::Pointer filter = FilterType::New();

  std::cout << "Testing the CastImageFilter, CPU vs GPU:\n";
  std::cout << "CPU/GPU splineOrder #threads time RMSE\n";

  // Time the filter, run on the CPU
  itk::TimeProbe cputimer;
  cputimer.Start();
  for( unsigned int i = 0; i < runTimes; i++ )
  {
    filter->SetInput( reader->GetOutput() );
    try{ filter->Update(); }
    catch( itk::ExceptionObject & e )
    {
      std::cerr << "ERROR: " << e << std::endl;
      return EXIT_FAILURE;
    }
    filter->Modified();
  }
  cputimer.Stop();

  std::cout << "CPU " 
    << filter->GetNumberOfThreads()
    << " " << cputimer.GetMean() / runTimes << std::endl;

  /** Write the CPU result. */
  WriterType::Pointer writer = WriterType::New();
  writer->SetInput( filter->GetOutput() );
  writer->SetFileName( outputFileNameCPU.c_str() );
  try{ writer->Update(); }
  catch( itk::ExceptionObject & e )
  {
    std::cerr << "ERROR: " << e << std::endl;
    return EXIT_FAILURE;
  }

  // Register object factory for GPU image and filter
  // All these filters that are constructed after this point are
  // turned into a GPU filter.
  itk::ObjectFactoryBase::RegisterFactory( itk::GPUImageFactory::New() );
  itk::ObjectFactoryBase::RegisterFactory( itk::GPUCastImageFilterFactory::New() );
  
  // Construct the filter
  // Use a try/catch, because construction of this filter will trigger
  // OpenCL compilation, which may fail.
  FilterType::Pointer gpuFilter;
  try{ gpuFilter = FilterType::New(); }
  catch( itk::ExceptionObject & e )
  {
    std::cerr << "ERROR: " << e << std::endl;
    return EXIT_FAILURE;
  }

  // Also need to re-construct the image reader, so that it now
  // reads a GPUImage instead of a normal image.
  // Otherwise, you will get an exception when running the GPU filter:
  // "ERROR: The GPU InputImage is NULL. Filter unable to perform."
  ReaderType::Pointer gpuReader = ReaderType::New();
  gpuReader->SetFileName( inputFileName );

  // Time the filter, run on the GPU
  itk::TimeProbe gputimer;
  gputimer.Start();
  for( unsigned int i = 0; i < runTimes; i++ )
  {
    gpuFilter->SetInput( gpuReader->GetOutput() );
    try{ gpuFilter->Update(); }
    catch( itk::ExceptionObject & e )
    {
      std::cerr << "ERROR: " << e << std::endl;
      return EXIT_FAILURE;
    }
    // Due to some bug in the ITK synchronisation we now manually
    // copy the result from GPU to CPU, without calling Update() again,
    // and not clearing GPU memory afterwards.
    itk::GPUExplicitSync<FilterType, OutputImageType>( gpuFilter, false, false );
    //itk::GPUExplicitSync<FilterType, ImageType>( gpuFilter, false, true ); // crashes!
    gpuFilter->Modified();
  }
  // GPU buffer has not been copied yet, so we have to make manual update
  //itk::GPUExplicitSync<FilterType, ImageType>( gpuFilter, false, true );
  gputimer.Stop();

  std::cout << "GPU x " << gputimer.GetMean() / runTimes;

  /** Write the GPU result. */
  WriterType::Pointer gpuWriter = WriterType::New();
  gpuWriter->SetInput( gpuFilter->GetOutput() );
  gpuWriter->SetFileName( outputFileNameGPU.c_str() );
  try{ gpuWriter->Update(); }
  catch( itk::ExceptionObject & e )
  {
    std::cerr << "ERROR: " << e << std::endl;
    return EXIT_FAILURE;
  }

  // Compute RMSE
  itk::ImageRegionConstIterator<OutputImageType> cit(
    filter->GetOutput(), filter->GetOutput()->GetLargestPossibleRegion() );
  itk::ImageRegionConstIterator<OutputImageType> git(
    gpuFilter->GetOutput(), gpuFilter->GetOutput()->GetLargestPossibleRegion() );

  double rmse = 0.0;
  for( cit.GoToBegin(), git.GoToBegin(); !cit.IsAtEnd(); ++cit, ++git )
  {
    double err = static_cast<double>( cit.Get() ) - static_cast<double>( git.Get() );
    rmse += err * err;
  }
  rmse = vcl_sqrt( rmse / filter->GetOutput()->GetLargestPossibleRegion().GetNumberOfPixels() );
  std::cout << " " << rmse << std::endl;

  // Check
  if( rmse > epsilon )
  {
    std::cerr << "ERROR: RMSE between CPU and GPU result larger than expected" << std::endl;
    return EXIT_FAILURE;
  }

  // End program.
  return EXIT_SUCCESS;

} // end main()