/**************************************************************************************************
**************************************************************************************************

GPL-3 License (https://www.tldrlegal.com/l/gpl-3.0)

Copyright (c) 2015 Andr��s Sol��s Montero <http://www.solism.ca>, All rights reserved.

sKCF is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

**************************************************************************************************
**************************************************************************************************/
#include "ktrackers.h"
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>  
#include <opencv2/features2d/features2d.hpp>  
#include <opencv2/opencv.hpp>
#include <vector>


using namespace std;
using namespace cv;
void KTrackers::setArea(const RotatedRect &rect,const Rect &boundingBox)
{

	_target.initiated = false;
	_target.size =rect.size;
	_target.center = rect.center;
	alpha = rect.angle;
	
	//int w = getOptimalDFTSize(floor(_target.size.width  * ( 1 + _params.padding)));//getOptimalDFTSize
	//int h = getOptimalDFTSize(floor(_target.size.height * ( 1 + _params.padding)));//getOptimalDFTSize
	int w = (floor(boundingBox.width* (1 + _params.padding)));//getOptimalDFTSize
	int h = (floor(boundingBox.height *(1 + _params.padding)));//getOptimalDFTSize
	_target.windowSize = Size(w, h);
	_target.model_xf.clear();
	_target.model_alphaf = Mat();
}

void KTrackers::getTrackedArea(vector<Point2f> &pts)
{
	pts.resize(4);
	RotatedRect area(_target.center, _target.size, alpha);
	area.points(&pts[0]);

}

void KTrackers::getPoints(
	const Mat& image,
	const Mat& patch,
	const ConfigParams &params,
	const TObj &obj,
	vector<Point2f> &points,
	Point2f &patchTL)
{
	//if params.points detector type
	//Shi-Tomasi corner detector
	assert(patch.type() == CV_32FC1 || patch.type() == CV_8UC1);

	Rect iRoi(0, 0, image.cols, image.rows);
	Rect tRoi(obj.center.x - floor(obj.windowSize.width / 2), obj.center.y - floor(obj.windowSize.height / 2),
		obj.windowSize.width, obj.windowSize.height);
	Rect fRoi = iRoi & tRoi;
	patchTL = fRoi.tl();

	double qualityLevel = 0.01;
	double minDistance = 3;
	int       blockSize = 3;
	bool useHarrisDetector = false;
	double k = 0.04;
	int maxCorners = 100;
	Point tl((patch.cols - obj.size.width) / 2.0,
		(patch.rows - obj.size.height) / 2.0);
	Point br(tl + Point(obj.size.width, obj.size.height));
	Mat mask = Mat::zeros(patch.size(), CV_8UC1);
	rectangle(mask, tl, br, Scalar(255), CV_FILLED);
	goodFeaturesToTrack(patch,
		points,
		maxCorners,
		qualityLevel,
		minDistance,
		mask,
		blockSize,
		useHarrisDetector,
		k);
}
//step-1
void KTrackers::processFrame(const cv::Mat &frame)
{
	Mat patch, filter;
	Mat kf, yf, kzf, alphaf;
	vector<Mat> xf, zf;
	bool adjust = false;
	Size sz(_target.windowSize.width / _params.cell_size,
		_target.windowSize.height / _params.cell_size);
	Size2d tsz;
	Point2f tmpMotion = (0, 0);
	if(!_params.rotation&& !_params.scale)
		tsz=Point2d(min((double)sz.width, _target.size.width/ _params.cell_size),
			min((double)sz.height, _target.size.height/ _params.cell_size));
	else
	{
		if ((_target.windowSize.width < _target.windowSize.height) && (_target.size.width > _target.size.height))
		{
			tsz = Point2d(min((double)sz.width, _target.size.height / _params.cell_size),
				min((double)sz.height, _target.size.width / _params.cell_size));
			adjust = true;

		}
		else
			tsz = Point2d(min((double)sz.width, _target.size.width / _params.cell_size),
				min((double)sz.height, _target.size.height / _params.cell_size));
	}
	
	double maxVal = 0;
	//_target.time = clock() - _target.time;
	//cout << _target.time << endl;
	//_target.time = clock();
	//_params.kernel_feature = KFeat::DEEP;
	if (_target.initiated)
	{
		Point2f shift;
		KTrackers::getPatch(frame, _target.center, _target.windowSize, patch);// patch is the windowed target (x2.5 scale) , if near the edge, the patch will randomly get so,e pixel
																			  //cv::imshow("patch", patch)																	  //KTrackers::writeMatData(filter);
		if (_params.scale || _params.rotation)
			KTrackers::getFeatures(patch, _params, _gaussianFilter, zf,_target.kernels);
		else
		{
			KTrackers::hannWindow(sz, filter);
			KTrackers::getFeatures(patch, _params, filter, zf,_target.kernels);
		}
		KTrackers::fft2(zf, _params);

		switch (_params.kernel_type)
		{
		case KType::GAUSSIAN:
		{
			KTrackers::gaussian_correlation(zf, _target.model_xf, _params, kzf, false);
			break;
		}
		case KType::POLYNOMIAL:
		{
			KTrackers::polynomial_correlation(zf, _target.model_xf, _params, kzf);
			break;
		}
		case KType::LINEAR:
		{
			KTrackers::linear_correlation(zf, _target.model_xf, kzf);
			break;
		}
		}
		maxVal=KTrackers::fastDetection(_target.model_alphaf, kzf, shift);
		Point2f _shift(_params.cell_size * Point2f(shift.x, shift.y));
		_target.center = _target.center + _shift;
		if (_target.center.x<0 || _target.center.x>frame.cols||_target.center.y<0 || _target.center.y>frame.rows)
			_target.center = _target.center -_shift;
		tmpMotion = _shift;
		//_target.center = _target.center + _shift;
		//cout << maxVal;
		if (_params.scale || _params.rotation) // we need to get keypoint pair in scale or rotate estimation
		{
			_flow.processFrame(patch, filter, _target.size, _shift, _target.center);
			double scale = _flow.getScale();
			//cout << scale << endl;
			if (!_params.scale)
				scale = 1.0;
			if(adjust==false)
				_target.size = Size2d(min((double)_target.windowSize.width, (_target.size.width * scale)),
				min((double)_target.windowSize.height, (_target.size.height * scale)));
			else
				_target.size = Size2d(min((double)_target.windowSize.height, (_target.size.width * scale)),
					min((double)_target.windowSize.width, (_target.size.height * scale)));
			
		}


	}
	


	float sigma = sqrt(_target.size.width * _target.size.height) *
		_params.output_sigma_factor / _params.cell_size;
	float sigmaW, sigmaH;

	sigmaW = (float)tsz.width / (float)sz.width;
	sigmaH = (float)tsz.height / (float)sz.height;
	//for rotation, we need to update angle, for scale ,we never change angle
	if(_params.rotation)
		alpha = alpha + _flow.getAngle()*180/CV_PI;
	if(adjust==false)
 		KTrackers::gaussianWindowRotation(sz, sigmaW, sigmaH, filter, alpha);
	else
		KTrackers::gaussianWindowRotation(sz, sigmaW, sigmaH, filter, alpha+90);
	
	_gaussianFilter = filter;
	KTrackers::gaussian_shaped_labels(sigma, sz, yf);
	
	
	KTrackers::fft2(yf, _params);

	KTrackers::getPatch(frame, _target.center, _target.windowSize, patch);
	


	if (_params.scale || _params.rotation)
	{
		_flow.extractPoints(patch, _target.size, _gaussianFilter);

		_ptl.x = _target.center.x - floor(_target.windowSize.width / 2);
		_ptl.y = _target.center.y - floor(_target.windowSize.height / 2);
	}
	//    else
	//    {
	//        //original KCF
	//        KTrackers::hannWindow(sz, filter);
	//    }

	// to load alexnet



	/* This is to initialize the AlexNet weights of first layer. These parameters are stored in "convfile.txt"
	  Note that this file is generated from another python scripts. From the code , we know that for every kernel
	  The size is 11x11*/
	if (!_target.initiated)
	{
		char line[256];
		float tempStore = 0;
		ifstream convW("convfile.txt");
		while (convW.good())
		{
			Mat singleKernel = Mat::zeros(11, 11, CV_32F);
			for (int i = 0; i < 11; i++)
			{
				convW.getline(line, 256);
				istringstream iss(line);
				for (int j = 0; j < 11; j++)
				{
					iss >> tempStore;
					singleKernel.at<float>(i, j) = tempStore;
				}
			}
			_target.kernels.push_back(singleKernel);
		}
		convW.close();

		
	
	}
	KTrackers::getFeatures(patch, _params, filter, xf, _target.kernels);
	KTrackers::fft2(xf, _params);

	switch (_params.kernel_type)
	{
	case KType::GAUSSIAN:
	{
		KTrackers::gaussian_correlation(xf, xf, _params, kf, true);
		break;
	}
	case KType::POLYNOMIAL:
	{
		KTrackers::polynomial_correlation(xf, xf, _params, kf);
		break;
	}
	case KType::LINEAR:
	{
		KTrackers::linear_correlation(xf, xf, kf);
		break;
	}
	}
	KTrackers::fastTraining(yf, kf, _params, alphaf);

	/* This is to show Mix of Gaussian way to do foreground background substraction
	Based on the priciple, this way can be only used in static background.
	{
		Mat fmask;
		_target.fgbg->apply(frame, fmask);
		imshow("mask", fmask);
	}
	*/

	if (!_target.initiated)
	{
		_target.model_xf = xf;
		_target.model_alphaf = alphaf;
		_target.initiated = true;
	}
	else
	{
		KTrackers::learn(_target.model_xf, xf, _target.model_alphaf, alphaf, _params);
		/*
		This part is trying to find a way to solve occlusion. We simply use maxVal as the indicator
		(which is not the right way). If maxVal is too low, we think a detection failure happens.
		In that case, we stop updating model until we find the problem solved. 

		So there are two problem remained.
		1. How to find if this frame failed 
		2. If we detect a failure, what should we do?
		This should be remained for future research.

		if (motion.x == 0 && motion.y == 0)
		{
			motion = tmpMotion;
			KTrackers::learn(_target.model_xf, xf, _target.model_alphaf, alphaf, _params);
		}
		else if(maxVal>0.3)
		{
			motion = 0.9*motion + 0.1*tmpMotion;
			KTrackers::learn(_target.model_xf, xf, _target.model_alphaf, alphaf, _params);
		}
		else
		{
			_target.center -= tmpMotion;
			_target.center +=0.5* motion;
			
			
		}
		*/
		
	}



}


KTrackers::KTrackers(KType type, KFeat feat, bool scale, bool rotation) :
	_target(), _params(type, scale), _ptl(0., 0.)
{

	switch (feat) {
	case KFeat::FHOG:
	{
		_params = FHOGConfigParams(type, scale, rotation);
		break;
	}
	case KFeat::DEEP:
	{
		_params = DeepConfigParams(type, scale, rotation);
		break;
	}
	case KFeat::GRAY:
	{
		_params = GrayConfigParams(type, scale);
		break;
	}
	case KFeat::RGB:
	{
		_params = RGBConfigParams(type, scale);
		break;
	}
	case KFeat::HLS:
	{
		_params = HLSConfigParams(type, scale);
		break;
	}
	case KFeat::HSV:
	{
		_params = HSVConfigParams(type, scale);
		break;
	}
	}
}




void KTrackers::divSpectrums(InputArray _srcA, InputArray _srcB,
	OutputArray _dst, int flags, bool conjB, double lambda)
{
	//lambda is a regularization term. avoid division by 0
	Mat srcA = _srcA.getMat(), srcB = _srcB.getMat();
	int depth = srcA.depth(), cn = srcA.channels(), type = srcA.type();
	int rows = srcA.rows, cols = srcA.cols;
	int j, k;

	CV_Assert(type == srcB.type() && srcA.size() == srcB.size());
	CV_Assert(type == CV_32FC1 || type == CV_32FC2 || type == CV_64FC1 || type == CV_64FC2);

	_dst.create(srcA.rows, srcA.cols, type);
	Mat dst = _dst.getMat();

	bool is_1d = (flags & DFT_ROWS) || (rows == 1 || (cols == 1 &&
		srcA.isContinuous() && srcB.isContinuous() && dst.isContinuous()));

	if (is_1d && !(flags & DFT_ROWS))
		cols = cols + rows - 1, rows = 1;

	int ncols = cols*cn;
	int j0 = cn == 1;
	int j1 = ncols - (cols % 2 == 0 && cn == 1);

	if (depth == CV_32F)
	{
		const float* dataA = (const float*)srcA.data;
		const float* dataB = (const float*)srcB.data;
		float* dataC = (float*)dst.data;

		size_t stepA = srcA.step / sizeof(dataA[0]);
		size_t stepB = srcB.step / sizeof(dataB[0]);
		size_t stepC = dst.step / sizeof(dataC[0]);

		if (!is_1d && cn == 1)
		{
			for (k = 0; k < (cols % 2 ? 1 : 2); k++)
			{
				if (k == 1)
					dataA += cols - 1, dataB += cols - 1, dataC += cols - 1;
				dataC[0] = saturate_cast<float>(dataA[0] / (dataB[0] + lambda));
				if (rows % 2 == 0)
					dataC[(rows - 1)*stepC] = saturate_cast<float>(dataA[(rows - 1)*stepA] / (dataB[(rows - 1)*stepB] + lambda));
				if (!conjB)
					for (j = 1; j <= rows - 2; j += 2)
					{
						//Ia = a + bi, Ib = b + ci
						//Ia/Ib = (a + bi) * ( c - di) / (c^2 + d^2);
						//den = c^2 + d^2
						//re = ac + bd / den
						//im = cb - ad / den

						double _a = (double)dataA[j*stepA];
						double _b = (double)dataA[(j + 1)*stepA];
						double _c = (double)dataB[j*stepB];
						double _d = (double)dataB[(j + 1)*stepB];
						double den = (_c + lambda) * (_c + lambda) + (_d * _d);
						double re = _a * _c + _b * _d;
						double im = _b * _c - _a * _d;
						dataC[j*stepC] = saturate_cast<float>((re / den));
						dataC[(j + 1)*stepC] = saturate_cast<float>((im / den));
					}
				else
					for (j = 1; j <= rows - 2; j += 2)
					{
						double _a = (double)dataA[j*stepA];
						double _b = (double)dataA[(j + 1)*stepA];
						double _c = (double)dataB[j*stepB];
						double _d = (double)dataB[(j + 1)*stepB];
						double den = (_c + lambda) * (_c + lambda) + (_d * _d);
						double re = _a * _c - _b * _d;
						double im = _a * _d + _b * _c;

						dataC[j*stepC] = saturate_cast<float>(re / den);
						dataC[(j + 1)*stepC] = saturate_cast<float>(im / den);
					}
				if (k == 1)
					dataA -= cols - 1, dataB -= cols - 1, dataC -= cols - 1;
			}
		}

		for (; rows--; dataA += stepA, dataB += stepB, dataC += stepC)
		{
			if (is_1d && cn == 1)
			{
				dataC[0] = dataA[0] / (dataB[0] + lambda);
				if (cols % 2 == 0)
					dataC[j1] = dataA[j1] / (dataB[j1] + lambda);
			}

			if (!conjB)
				for (j = j0; j < j1; j += 2)
				{
					//Ia = a + bi, Ib = b + ci
					//Ia/Ib = (a + bi) * ( c - di) / (c^2 + d^2);
					//den = c^2 + d^2
					//re = ac + bd / den
					//im = cb - ad / den
					double _a = (double)dataA[j];
					double _b = (double)dataA[j + 1];
					double _c = (double)dataB[j];
					double _d = (double)dataB[j + 1];
					double den = (_c + lambda) * (_c + lambda) + (_d * _d);
					double re = _a * _c + _b * _d;
					double im = _b * _c - _a * _d;

					dataC[j] = saturate_cast<float>(re / den);
					dataC[j + 1] = saturate_cast<float>(im / den);
				}
			else
				for (j = j0; j < j1; j += 2)
				{
					double _a = (double)dataA[j];
					double _b = (double)dataA[j + 1];
					double _c = (double)dataB[j];
					double _d = (double)dataB[j + 1];
					double den = (_c + lambda) * (_c + lambda) + (_d * _d);
					double re = _a * _c - _b * _d;
					double im = _a * _d + _b * _c;
					dataC[j] = saturate_cast<float>(re / den);
					dataC[j + 1] = saturate_cast<float>(im / den);
				}
		}
	}
	else
	{
		const double* dataA = (const double*)srcA.data;
		const double* dataB = (const double*)srcB.data;
		double* dataC = (double*)dst.data;

		size_t stepA = srcA.step / sizeof(dataA[0]);
		size_t stepB = srcB.step / sizeof(dataB[0]);
		size_t stepC = dst.step / sizeof(dataC[0]);

		if (!is_1d && cn == 1)
		{
			for (k = 0; k < (cols % 2 ? 1 : 2); k++)
			{
				if (k == 1)
					dataA += cols - 1, dataB += cols - 1, dataC += cols - 1;
				dataC[0] = saturate_cast<double>(dataA[0] / (dataB[0] + lambda));
				if (rows % 2 == 0)
					dataC[(rows - 1)*stepC] = saturate_cast<double>(dataA[(rows - 1)*stepA] / (dataB[(rows - 1)*stepB] + lambda));
				if (!conjB)
					for (j = 1; j <= rows - 2; j += 2)
					{
						double _a = dataA[j*stepA];
						double _b = dataA[(j + 1)*stepA];
						double _c = dataB[j*stepB];
						double _d = dataB[(j + 1)*stepB];
						double den = (_c + lambda) * (_c + lambda) + (_d * _d);
						double re = _a * _c + _b * _d;
						double im = _b * _c - _a * _d;

						dataC[j*stepC] = saturate_cast<double>(re / den);
						dataC[(j + 1)*stepC] = saturate_cast<double>(im / den);
					}
				else
					for (j = 1; j <= rows - 2; j += 2)
					{
						double _a = dataA[j*stepA];
						double _b = dataA[(j + 1)*stepA];
						double _c = dataB[j*stepB];
						double _d = dataB[(j + 1)*stepB];
						double den = (_c + lambda) * (_c + lambda) + (_d * _d);
						double re = _a * _c - _b * _d;
						double im = _a * _d + _b * _c;

						dataC[j*stepC] = saturate_cast<double>(re / den);
						dataC[(j + 1)*stepC] = saturate_cast<double>(im / den);
					}
				if (k == 1)
					dataA -= cols - 1, dataB -= cols - 1, dataC -= cols - 1;
			}
		}

		for (; rows--; dataA += stepA, dataB += stepB, dataC += stepC)
		{
			if (is_1d && cn == 1)
			{
				dataC[0] = saturate_cast<double>(dataA[0] / (dataB[0] + lambda));
				if (cols % 2 == 0)
					dataC[j1] = saturate_cast<double>(dataA[j1] / (dataB[j1] + lambda));
			}

			if (!conjB)
				for (j = j0; j < j1; j += 2)
				{
					double _a = dataA[j];
					double _b = dataA[j + 1];
					double _c = dataB[j];
					double _d = dataB[j + 1];
					double den = (_c + lambda) * (_c + lambda) + (_d * _d);
					double re = _a * _c + _b * _d;
					double im = _b * _c - _a * _d;

					dataC[j] = saturate_cast<double>(re / den);
					dataC[j + 1] = saturate_cast<double>(im / den);

				}
			else
				for (j = j0; j < j1; j += 2)
				{
					double _a = dataA[j];
					double _b = dataA[j + 1];
					double _c = dataB[j];
					double _d = dataB[j + 1];
					double den = (_c + lambda) * (_c + lambda) + (_d * _d);
					double re = _a * _c - _b * _d;
					double im = _a * _d + _b * _c;
					dataC[j] = saturate_cast<double>(re / den);
					dataC[j + 1] = saturate_cast<double>(im / den);
				}
		}
	}
}





void KTrackers::fastTraining(const Mat &yf,
	const Mat &kf,
	const ConfigParams& params,
	Mat &alphaf)
{
	//alphaf = yf ./ (kf + lambda);
	divSpectrums(yf, kf, alphaf, 0, false, params.lambda);
}



void KTrackers::learn(vector<Mat> &modelXf, const vector<Mat> &xf,
	Mat         &modelAlphaF, const Mat &alphaf,
	const ConfigParams& params)
{
	assert(xf.size() == modelXf.size());
	addWeighted(modelAlphaF, (1.0 - params.interp_factor), alphaf,
		params.interp_factor, 0, modelAlphaF);

	auto weightPara = [&](const Range &r) {
		for (size_t i = r.start; i != r.end; ++i)
		{
			addWeighted(modelXf[i], (1.0 - params.interp_factor), xf[i],
				params.interp_factor, 0, modelXf[i]);
		}
	};
	weightPara(Range(0, xf.size()));
}

double KTrackers::fastDetection(const Mat &modelAlphaF, const Mat &kzf, Point2f &shift)
{
	
	Mat response, spatial;
	mulSpectrums(modelAlphaF, kzf, response, 0, false);
	idft(response, spatial, DFT_SCALE | DFT_REAL_OUTPUT);
	double minVal; double maxVal; Point minLoc; Point maxLoc;
	minMaxLoc(spatial, &minVal, &maxVal, &minLoc, &maxLoc);

	if (maxLoc.y > kzf.rows / 2)
		maxLoc.y -= kzf.rows;
	if (maxLoc.x > kzf.cols / 2)
		maxLoc.x -= kzf.cols;
	shift = maxLoc;
	return maxVal;
}

double KTrackers::fastDetectionIntepolation(const Mat &modelAlphaF, const Mat &kzf, Point2f &shift)
{
	Mat response, spatial;
	mulSpectrums(modelAlphaF, kzf, response, 0, false);
	idft(response, spatial, DFT_SCALE | DFT_REAL_OUTPUT);
	double minVal; double maxVal; Point minLoc, maxLoc;
	Point2d realLoc;
	Mat reshapeSpatial;
	spatial.copyTo(reshapeSpatial);
	float *spaData = (float*)reshapeSpatial.data;
	float *originData = (float*)spatial.data;
	// this part is to reshape the matrix.
	for (int i = 0; i<spatial.rows; i++)
		for (int j = 0; j < spatial.cols; j++)
		{
			int tempi = i;
			int tempj = j;
			if (tempi >= spatial.rows / 2)
			{
				tempi -= spatial.rows / 2;
			}
			else
			{
				if (spatial.rows % 2 == 0)
					tempi += spatial.rows / 2;
				else
					tempi += spatial.rows / 2 + 1;

			}
			if (tempj >= spatial.cols / 2)
			{
				tempj -= spatial.cols / 2;
			}
			else
			{
				if (spatial.cols % 2 == 0)
					tempj += spatial.cols / 2;
				else
					tempj += spatial.cols / 2 + 1;
			}
			*(spaData + tempi * spatial.cols + tempj) = *(originData + i * spatial.cols + j);
		}

	minMaxLoc(reshapeSpatial, &minVal, &maxVal, &minLoc, &maxLoc); //find the point where we should use the intepolation
																
	findLocalMaximum(reshapeSpatial, maxLoc.x, maxLoc.y, realLoc); 

	if (spatial.cols % 2 == 0)
	{
		maxLoc.x -= spatial.cols / 2;
	}
	else
	{
		maxLoc.x -= (spatial.cols / 2 + 1);
	}
	if (spatial.rows % 2 == 0)
	{
		maxLoc.y -= spatial.rows / 2;
	}
	else
	{
		maxLoc.y -= (spatial.rows / 2 + 1);
	}
	shift.x = maxLoc.x + realLoc.x;
	shift.y = maxLoc.y + realLoc.y;
	//add the  intepolation shift to the original maximum response location
	//cout << "(x,y): " << shift.x << shift.y << endl;
	return maxVal;
}

void  KTrackers::gaussianWindow(const Size &sz, float sigmaW, float sigmaH, Mat &filter)
{
	int width = sz.width;
	int height = sz.height;
	filter.create(sz, CV_32FC1);//Mat::zeros(sz, CV_32FC1); no need for zero initializing
	float *w = new float[width];
	float *h = new float[height];
	float wN = (float)(width - 1.) / 2.;
	float wH = (float)(height - 1.) / 2.;

	for (size_t i = 0; i < width; ++i)
	{
		float e = (i - wN) / (sigmaW * wN);
		w[i] = exp(-.5*e*e);
	}
	for (size_t i = 0; i < height; ++i)
	{
		float e = (i - wH) / (sigmaH * wH);
		h[i] = exp(-.5*e*e);
	}
	float *data = (float*)filter.data;
	auto gauss = [&](const Range &r)
	{
		size_t cW = (r.start % width), cH = (r.start / width);
		for (size_t i = r.start; i != r.end; ++i, ++cW)
		{
			if (cW >= width) { cW = 0; ++cH; }
			data[i] = w[cW] * h[cH];
		}
	};
	gauss(Range(0, width * height));
	//parallel_for_(Range(0,width * height), ParallelFunction(gauss));
	delete[]w;
	delete[]h;
}

/* this function will rotate the Gaussian window based on the angle alpha
 alpha is an anti-clockwise angle*/
void  KTrackers::gaussianWindowRotation(const Size &sz, float sigmaW, float sigmaH, Mat &filter, double alpha)
{
	int width = sz.width;
	int height = sz.height;
	filter.create(sz, CV_32FC1);//Mat::zeros(sz, CV_32FC1); no need for zero initializing
	float wN = (float)(width - 1.) / 2.;
	float wH = (float)(height - 1.) / 2.;
	alpha = alpha / 180 * CV_PI;
	double cosA = cos(alpha);
	double sinA = sin(alpha);
	float *data = (float*)filter.data;
	for (size_t i = 0; i < height; i++)
		for (size_t j = 0; j < width; j++)
		{
			double y = (cosA*(j - wN) + sinA*(i - wH)) / wN;
			double x = (cosA*(i - wH) - sinA*(j - wN)) / wH;
			double e1 = (x) / sigmaW;
			double e2 = (y) / sigmaH;
			float result = exp(-.5*(e1*e1 + e2*e2));
			data[i*width + j] = result;
		}
}

void KTrackers::findLocalMaximum(const Mat &response, const int x, const int y, Point2d &realLoc)
{

	float sx = 0;
	float sy = 0;//init the real shift & x,y is the discrete maximum
	int height = response.rows;
	int width = response.cols;
	float *data = (float *)response.data;
	float dx = (*(data + x + y*width + 1) - *(data + x + y*width - 1)) / 2;
	float dy = (*(data + x + (y + 1)*width) - *(data + x + (y - 1)*width)) / 2; // get derivD/derivX
	float H[4];//init the hessian metrix
	float dxx;
	float dyy;
	float dxy;
	//this is just like deriv^2D/derivX^2 and so on....
	dxx = (*(data + x + y*width + 1) + *(data + x + y*width - 1)) - 2 * (*(data + x + y*width));
	dyy = (*(data + x + (y + 1)*width) - *(data + x + (y - 1)*width)) - 2 * (*(data + x + y*width));
	dxy = (*(data + x + (y + 1)*width + 1) + *(data + x + (y - 1)*width - 1) - *(data + x + (y - 1)*width + 1) - *(data + x + (y + 1)*width - 1)) / 4;
	H[0] = dxx;
	H[1] = dxy;
	H[2] = dxy;
	H[3] = dyy; //dxx,dxy,dyx,dyy
	float H_inverse[4];// get the inverse of hessian
	float parameter = 1 / (H[0] * H[3] - H[1] * H[2]);
	H_inverse[0] = parameter* H[3];
	H_inverse[1] = parameter *(-H[1]);
	H_inverse[2] = parameter*(-H[2]);
	H_inverse[3] = parameter*H[0];
	// we can get the inverse of a matrix based on original hessian matrix
	//above is just to calculate the inverse matrix, H*H_inverse=I.
	sx = -(H_inverse[0] * dx + H_inverse[1] * dy);
	sy = -(H_inverse[2] * dx + H_inverse[3] * dy);
	realLoc.x = sx;
	realLoc.y = sy;
	//cout << realLoc << endl;

}

void KTrackers::gaussian_shaped_labels(float sigmaW, float sigmaH, const Size &sz, Mat &labels)
{
	float *trs = new float[sz.height];
	float *tcs = new float[sz.width];

	float *rs = new float[sz.height];
	float *cs = new float[sz.width];
	float wW = -1.0 / (2 * (sigmaW * sigmaW));
	float wH = -1.0 / (2 * (sigmaH * sigmaH));
	labels.create(sz, CV_32FC1);// Mat::zeros(sz, CV_32FC1);

	float w2 = floor(sz.width / 2);
	float h2 = floor(sz.height / 2);



	for (size_t i = 0; i < sz.width; ++i) {
		tcs[i] = (i + 1) - w2;
	}

	for (size_t i = 0; i < sz.height; ++i) {
		trs[i] = (i + 1) - h2;
	}

	for (size_t i = 0, j = (w2 - 1); i < sz.width; ++i, ++j)
	{
		if (j >= sz.width) j = 0;
		cs[i] = exp(wW * tcs[j] * tcs[j]);
	}

	for (size_t i = 0, j = (h2 - 1); i < sz.height; ++i, ++j)
	{
		if (j >= sz.height) j = 0;
		rs[i] = exp(wH * trs[j] * trs[j]);
	}

	float *data = (float*)labels.data;
	auto gauss = [&](const Range &r) {
		size_t cW = (r.start % sz.width), cH = (r.start / sz.width);
		for (size_t i = r.start; i != r.end; ++i, ++cW) {
			if (cW >= sz.width) { cW = 0; ++cH; }
			data[i] = rs[cH] * cs[cW];
		}
	};
	
	gauss(Range(0, sz.width  * sz.height));
	delete[]rs;
	delete[]cs;
	delete[]trs;
	delete[]tcs;
}

void KTrackers::gaussian_shaped_labels(float sigma, const Size &sz, Mat &labels)
{
	float *trs = new float[sz.height];
	float *tcs = new float[sz.width];

	float *rs = new float[sz.height];
	float *cs = new float[sz.width];
	float w = -1.0 / (2 * (sigma * sigma));
	labels.create(sz, CV_32FC1);// Mat::zeros(sz, CV_32FC1);

	float w2 = floor(sz.width / 2);
	float h2 = floor(sz.height / 2);



	for (size_t i = 0; i < sz.width; ++i) {
		tcs[i] = (i + 1) - w2;
	}

	for (size_t i = 0; i < sz.height; ++i) {
		trs[i] = (i + 1) - h2;
	}

	for (size_t i = 0, j = (w2 - 1); i < sz.width; ++i, ++j)
	{
		if (j >= sz.width) j = 0;
		cs[i] = exp(w * tcs[j] * tcs[j]);
		
	}

	for (size_t i = 0, j = (h2 - 1); i < sz.height; ++i, ++j)
	{
		if (j >= sz.height) j = 0;
		rs[i] = exp(w * trs[j] * trs[j]);
	}

	//    rs[0] = cs[0] = exp(0);
	//    size_t i;
	//    
	//    for (i = 1; i <= sz.height/2; ++i)
	//    {
	//        float v = exp(w * float(i) * float(i));
	//        rs[sz.height - i] = v;
	//        rs[i] = v;
	//    }
	//    if (sz.height % 2 == 1)
	//        rs[i] = exp(w * float(i) * float(i));
	//    for (i = 1; i <= sz.width/2; ++i)
	//    {
	//        float v = exp(w * float(i) * float(i));
	//        cs[sz.width - i] =  v;
	//        cs[i] = v;
	//    }
	//    if (sz.width % 2 == 1)
	//        cs[i] = exp(w * float(i) * float(i));


	float *data = (float*)labels.data;
	auto gauss = [&](const Range &r) {
		size_t cW = (r.start % sz.width), cH = (r.start / sz.width);
		for (size_t i = r.start; i != r.end; ++i, ++cW) {
			if (cW >= sz.width) { cW = 0; ++cH; }
				data[i] = rs[cH] * cs[cW];
		}
	};
	gauss(Range(0, sz.width  * sz.height));
	//Mat copy; 
	//labels.copyTo(copy);
	//imshow("2",copy);
	delete[]rs;
	delete[]cs;
	delete[]trs;
	delete[]tcs;
}

void KTrackers::gaussian_shaped_labels(float sigmaW,float sigmaH, const Size &sz, Mat &labels, float angle)
{
	int width = sz.width;
	int height = sz.height;
	labels.create(sz, CV_32FC1);//Mat::zeros(sz, CV_32FC1); no need for zero initializing
	Mat temp;
	labels.copyTo(temp);
	float wN = (float)(width - 1.) / 2.;
	float wH = (float)(height - 1.) / 2.;
	angle = angle / 180 * CV_PI;
	double cosA = cos(angle);
	double sinA = sin(angle);
	float *data = (float*)temp.data;
	float *newData = (float*)labels.data;
	for (size_t i = 0; i < height; i++)
		for (size_t j = 0; j < width; j++)
		{
			double y = (cosA*(j - wN) + sinA*(i - wH)) / wN;
			double x = (cosA*(i - wH) - sinA*(j - wN)) / wH;
			double e1 = 10*(x) / sigmaW;
			double e2 = 10*(y) / sigmaH;
			float result = exp(-.5*(e1*e1 + e2*e2));
			data[i*width + j] = result;
		}

	for (int i = 0; i<height; i++)
		for (int j = 0; j < width; j++)
		{
			int tempi = i;
			int tempj = j;
			if (tempi >= height / 2)
			{
				tempi -= height / 2;
			}
			else
			{
				if (height % 2 == 0)
					tempi += height / 2;
				else
					tempi += height / 2 + 1;

			}
			if (tempj >=width / 2)
			{
				tempj -= width / 2;
			}
			else
			{
				if (width % 2 == 0)
					tempj += width / 2;
				else
					tempj += width / 2 + 1;
			}
			*(newData + tempi*width + tempj) = *(data + i*width + j);
		}
	
}


//step-2 expand the border if target is partially out of frame.
void KTrackers::getPatch(const Mat& image, const Point2f &loc, const Size &sz, Mat &output)
{
	Rect iRoi(0, 0, image.cols, image.rows);
	Rect tRoi(loc.x - floor(sz.width / 2), loc.y - floor(sz.height / 2),
		sz.width, sz.height);
	Rect fRoi = iRoi & tRoi;
	output.create(sz, image.type());

	int top = 0, left = 0, bottom = 0, right = 0;

	Point tl = tRoi.tl();
	Point br = tRoi.br();

	if (tl.x < 0) left = min(-tl.x, sz.width);

	if (tl.y < 0) top = min(-tl.y, sz.height);

	if (br.x > image.cols) right = min(br.x - image.cols, sz.width);

	if (br.y > image.rows) bottom = min(br.y - image.rows, sz.height);
	if (br.y <= 0 || br.x <= 0 || tl.x >= image.cols || tl.y >= image.rows)
	{
		top = sz.height;
		left = sz.width;
		bottom = 0;
		right = 0;
	}

	copyMakeBorder(image(fRoi), output, top, bottom, left, right, BORDER_REPLICATE | BORDER_ISOLATED);
}

void  KTrackers::hannWindow(const Size &sz, Mat &filter)
{
	int width = sz.width;
	int height = sz.height;
	filter.create(sz, CV_32FC1);// Mat::zeros(sz, CV_32FC1);
	float *w = new float[width];
	float *h = new float[height];
	for (size_t i = 0; i < width; ++i)
		w[i] = .5 * (1. - cos((2.* CV_PI* i) / (width - 1)));
	for (size_t i = 0; i < height; ++i)
		h[i] = .5 * (1. - cos((2.* CV_PI* i) / (height - 1)));
	float *data = (float*)filter.data;
	auto hann = [&](const Range &r) {
		size_t cW = (r.start % width), cH = (r.start / width);
		for (size_t i = r.start; i != r.end; ++i, ++cW)
		{
			if (cW >= width) { cW = 0; ++cH; }
			data[i] = w[cW] * h[cH];
			//cout << data[i];
			//cout << " ";
		}
	};
	hann(Range(0, width * height));
	//cv::imshow("filter", filter);
	//parallel_for_(Range(0,width * height), ParallelFunction(hann));
	delete[]w;
	delete[]h;
}

void KTrackers::fft2(Mat &features, const ConfigParams &params)
{
	dft(features, features, params.flags);
}
void KTrackers::fft2(const Mat &features, Mat &fft2, const ConfigParams &params)
{
	dft(features, fft2, params.flags);
}

void KTrackers::fft2(const vector<Mat> &features, vector<Mat> &fft2, const ConfigParams &params)
{
	fft2.clear();
	fft2.resize(features.size());
	auto dftPara = [&](const Range &r) {
		for (size_t i = r.start; i != r.end; ++i)
		{
			dft(features[i], fft2[i], params.flags);
		}
	};
	dftPara(Range(0, features.size()));
}
void KTrackers::fft2(vector<Mat> &features, const ConfigParams &params)
{
	auto dftPara = [&](const Range &r) {
		for (size_t i = r.start; i != r.end; ++i)
		{
			dft(features[i], features[i], params.flags);
		}
	};
	dftPara(Range(0, features.size()));
}



double KTrackers::sumSpectrum(const Mat &mat, const ConfigParams &params)
{
	//CCS packed format only carries half of the info.
	//Top left value is not repeated when the spectrum is expanded
	if (params.flags == 0)
		return (sum(mat).val[0] * 2) - mat.at<float>(0, 0);
	else //DFT_COMPLEX_OUTPUT
	{
		return sum(mat).val[0];
	}
}
void KTrackers::polynomial_correlation(const vector<Mat> &xf,
	const vector<Mat> &yf,
	const ConfigParams &params,
	Mat &kf)
{
	Size size(xf[0].cols, xf[0].rows);
	double N = size.width * size.height * xf.size();
	Mat sumC = Mat::zeros(size, CV_32FC1);
	Mutex access;
	auto fPara = [&](const Range &r) {
		Mat _sumC = Mat::zeros(size, CV_32FC1);
		for (size_t i = r.start; i != r.end; ++i)
		{
			Mat response, spatial;
			//cross-correlation term in Fourier domain
			mulSpectrums(xf[i], yf[i], response, 0, true);
			//inverse = real(ifft2(response)) back to spatial domain
			idft(response, spatial, DFT_SCALE | DFT_REAL_OUTPUT);
			//sum the values from each channel
			add(_sumC, spatial, _sumC);
		}
		access.lock();
		add(sumC, _sumC, sumC);
		access.unlock();
	};
	fPara(Range(0, xf.size()));
	//    NonParallelVersion
	polynomialResponse<float>(sumC, N, params.kernel_poly_a, params.kernel_poly_b);
	dft(sumC, kf, params.flags);

}
void KTrackers::gaussian_correlation(const vector<Mat> &xf,
	const vector<Mat> &yf,
	const ConfigParams &params,
	Mat &kf,
	bool autocorrelation)
{
	double xx = 0, yy = 0;
	kf.create(xf[0].rows, xf[0].cols, xf[0].type()); //Mat::zeros(xf[0].rows, xf[0].cols, xf[0].type());
	Mat sumReal = Mat::zeros(xf[0].rows, xf[0].cols, CV_32FC1);
	long N = xf[0].rows * xf[0].cols;

	//speeding up the process when autocorrelation
	Mutex access;
	auto fPara = [&](const Range &r) {
		double _xx = 0, _yy = 0;
		Mat _sumReal = Mat::zeros(xf[0].rows, xf[0].cols, CV_32FC1);
		for (size_t i = r.start; i != r.end; ++i)
		{
			Mat response, spatial;
			//squared norm of x and y
			mulSpectrums(xf[i], xf[i], response, 0, true);
			_xx += sumSpectrum(response, params); //cv::sum(response).val[0];
			if (!autocorrelation)
			{
				mulSpectrums(yf[i], yf[i], response, 0, true);
				_yy += sumSpectrum(response, params); //cv::sum(response).val[0];
													  //cross-correlation term in Fourier domain
													  //response = xf .* conf(yf)
				mulSpectrums(xf[i], yf[i], response, 0, true);
			}
			else
				// response and yy are already computed
				_yy = _xx;
			//inverse = real(ifft2(response)) back to spatial domain
			idft(response, spatial, DFT_SCALE | DFT_REAL_OUTPUT);
			//sum the values from each channel
			add(_sumReal, spatial, _sumReal);
		}
		access.lock();
		xx = _xx;
		yy = _yy;
		add(sumReal, _sumReal, sumReal);
		access.unlock();
	};


	fPara(Range(0, xf.size()));
	xx /= N; // meanX
	yy /= N; // meanY

	double a = -1 / (params.kernel_sigma * params.kernel_sigma);
	double b = xx + yy;
	double c = (double)N * xf.size();

	gaussianResponse<float>(sumReal, a, b, c);
	dft(sumReal, kf, params.flags);
}


void KTrackers::linear_correlation(const vector<Mat> &xf,
	const vector<Mat> &yf,
	Mat &kf)
{
	Size size(xf[0].cols, xf[0].rows);
	double N = size.width * size.height * xf.size();
	kf = Mat::zeros(size, xf[0].type());
	Mutex access;
	auto fPara = [&](const Range &r) {
		Mat _kf = Mat::zeros(size, xf[0].type());
		for (size_t i = r.start; i != r.end; ++i)
		{
			Mat response;
			//cross-correlation term in Fourier domain
			mulSpectrums(xf[i], yf[i], response, 0, true);
			add(_kf, response, _kf);

		}
		access.lock();
		add(kf, _kf, kf);
		access.unlock();
	};
	fPara(Range(0, xf.size()));
	kf = kf / N;

}

void rgbNorm(Mat &input, Mat &output)
{
	output.create(input.size(), CV_32FC3);
	for (size_t r = 0; r < input.rows; r++)
	{
		float *rowI = input.ptr<float>(r);
		float *rowO = output.ptr<float>(r);
		for (size_t c = 0; c < input.cols * input.channels(); c += input.channels())
		{
			float a = rowI[c];
			float b = rowI[c + 1];
			float d = rowI[c + 2];
			float su = a + b + d;
			rowO[c] = a / su;
			rowO[c + 1] = b / su;
			rowO[c + 2] = d / su;
		}
	}
}


void KTrackers::getFeatures(const Mat& patch,
	const ConfigParams &params,
	const Mat& windowFunction,
	vector<Mat> &features,
    vector<Mat> &kernels)
{
	features.clear();

	
	//assert(patch.type() == CV_32F || patch.type() == CV_32FC3);
	switch (params.kernel_feature) {
	case (KFeat::HSV):
	{
		Mat color, hsv, floatImg;
		KFlow::toBGR(patch, color);

		cvtColor(color, hsv, CV_BGR2HSV_FULL);
		//range of HSV_FULL is 0-255 0-255 0-255
		//range of HSV      is 0-180 0-255 0-255

		hsv.convertTo(floatImg, CV_32F, 1.0 / 255.0);
		Scalar _mean = mean(floatImg);
		split(floatImg - _mean, features);
		cout << "HSV" << endl;


		break;
	}
	case (KFeat::HLS):
	{
		Mat color, hsv, floatImg;
		KFlow::toBGR(patch, color);
		//range of HLS_FULL is 0-255 0-255 0-255
		//range of HLS      is 0-180 0-255 0-255
		cvtColor(color, hsv, CV_BGR2HLS_FULL);
		hsv.convertTo(floatImg, CV_32F, 1.0 / 255.0);
		Scalar _mean = mean(floatImg);
		split(floatImg - _mean, features);
		cout << "HLS" << endl;
		break;
	}
	case (KFeat::GRAY):
	{
		Mat grayImg, floatImg;
		KFlow::toGray(patch, grayImg);
		grayImg.convertTo(floatImg, CV_32F, 1.0 / 255.0);
		Scalar _mean = mean(floatImg);
		split(floatImg - _mean, features);
		cout << "GRAY" << endl;
		break;
	}
	case (KFeat::RGB):
	{
		Mat floatImg;
		patch.convertTo(floatImg, CV_32F, 1.0 / 255.0);
		Scalar _mean = mean(floatImg);
		split(floatImg - _mean, features);
		cout << "RGB" << endl;
		break;
	}
	
	case (KFeat::FHOG):
	{
		Mat grayImg, floatImg;
		KFlow::toGray(patch, grayImg);
		grayImg.convertTo(floatImg, CV_32F, 1.0 / 255.0);
		//KTrackers::writeMatData(floatImg);
		fhog(floatImg, features, params.cell_size, params.hog_orientations);
		features.pop_back(); //last channel is only zeros
		break;
	}
	
	
	// This is a new feature extraction way. We use median layer of a deep network as feature rather than hand craft features
	case (KFeat::DEEP):
	{
		
		
		vector<Mat> splitImage;
		Mat szResultb, szResultg, szResultr,resizeImg;
		double width = patch.cols;
		double height = patch.rows;
		//resize(patch, resizeImg, windowFunction.size());
		resize(patch, resizeImg, Size(params.rs*width, params.rs*height));
		split(resizeImg, splitImage);
		//split(patch, splitImage);
		//imshow("B", splitImage[0]);
		//imshow("G", splitImage[1]);
		//imshow("R", splitImage[2]);
		int chan = 0;
		for (int i = 0; i <params.kNum;i++)
		{
			Mat fliped,result;
			flip(kernels[i], fliped, -1);
			filter2D(splitImage[chan], result, 1, fliped, Point(-1, -1));
			result.convertTo(result, CV_32F);
			switch (chan)
			{
			case 0:
				resize(result, szResultb, windowFunction.size());	
				//szResultb = result;
				break;
			case 1:
				resize(result, szResultg, windowFunction.size());
				//szResultg = result;
				break;
			case 2:
				resize(result, szResultr, windowFunction.size());
				//szResultr = result;
				break;
			}
			
			chan++;
			if (chan == 3)
			{
				Mat szResult;
				add(szResultb, szResultg, szResult);
				add(szResultr, szResult, szResult);
				normalize(szResult, szResult, 1, 0, NORM_L2);
				features.push_back(szResult);
				chan = 0;
			}
			
			
				

		}
		//imshow("origin", patch);
		//Mat fshow1, fshow2, fshow3;
		//resize(features[0], fshow1, windowFunction.size()*4);
		//resize(features[3], fshow2, windowFunction.size() * 4);
		//resize(features[6], fshow3, windowFunction.size() * 4);
		//imshow("k1", fshow1*3);
		//imshow("k2", fshow2*3);
		//imshow("k3", fshow3*3);
		//cout << "DEEP" << endl;
		break;
	}
	
	default:
	{
		break;
	}
	}
	auto fPara = [&](const Range &r) {
		for (size_t i = r.start; i != r.end; ++i)
		{
			features[i] = features[i].mul(windowFunction);
		}
	};
	fPara(Range(0, features.size()));
	//return features[0].size();
}


/*
* Computes the NCC value for points from one frame to the other
*/
void KFlow::NCC(const Mat &I,
	const Mat &J,
	vector<Point2f> &ptsI,
	vector<Point2f> &ptsJ,
	vector<uchar> &status,
	vector<float> &result,
	const KFlowConfigParams &p)
{
	Size patchSize(p.winsize_ncc, p.winsize_ncc);
	Mat recI(patchSize, CV_8UC1);
	Mat recJ(patchSize, CV_8UC1);
	vector<float> res;

	for (size_t i = 0; i < ptsI.size(); i++)
	{
		if (status[i])
		{
			getRectSubPix(I, patchSize, ptsI[i], recI);
			getRectSubPix(J, patchSize, ptsJ[i], recJ);
			matchTemplate(recI, recJ, res, p.method);
			result[i] = res[0];
		}
		else
			result[i] = 0.0f;
	}
}

/*
* tracks area B to BNew using two images frame I and J.
*/
void KFlow::flowForward(const Mat &I,
	const Mat &J,
	vector<Point2f> &from,
	vector<Point2f> &to,
	const KFlowConfigParams &p)
{
	vector<Point2f> points;
	vector<uchar>   accept[2];
	vector<float>      err[2]; //valuesNCC err[0]  //errorFB err[1]

	calcOpticalFlowPyrLK(I, J, from, to, accept[0], err[0], p.winLK, p.level, p.criteria);//CV_LKFLOW_INITIAL_GUESSES);

	NCC(I, J, from, to, accept[0], err[0], p);
	// NORM2(points[0],points[2], err[1]);

	int goodPts = 0;
	for (size_t i = 0; i < from.size(); i++)
	{
		if (accept[0][i])
		{
			from[goodPts] = from[i];
			to[goodPts] = to[i];
			//groups[goodPts] = groups[i];
			err[0][goodPts] = err[0][i];
			//  err[1][goodPts] = err[1][i];
			goodPts++;

		}
	}
	from.resize(goodPts);
	to.resize(goodPts);
	//groups.resize(goodPts);
	err[0].resize(goodPts);
	//err[1].resize(goodPts);


	float medNCC = getMedian(&err[0][0], (int)err[0].size());
	//        float medFB = getMedian(&err[1][0],(int)err[1].size());
	//
	//        if (medFB > medFBThreshold)
	//            return false;

	goodPts = 0;
	for (size_t i = 0; i < from.size(); i++)
	{
		if (err[0][i] >= medNCC)
		{
			from[goodPts] = from[i];
			to[goodPts] = to[i];
			//groups[goodPts] = groups[i];
			goodPts++;
		}
	}
	from.resize(goodPts);
	//groups.resize(goodPts);
	to.resize(goodPts);
}

void KFlow::flowForwardBackward(const Mat &I,
	const Mat &J,
	vector<Point2f> &from,
	vector<Point2f> &to,
	const KFlowConfigParams &p,
	vector<float> &weights)
{
	vector<Point2f> points;
	vector<uchar>   accept[2];
	vector<float>      err[2]; //valuesNCC err[0]  //errorFB err[1]
	vector<float> newWeights[2];//record the weights that match the good points

	calcOpticalFlowPyrLK(I, J, from, to, accept[0], err[0], p.winLK, p.level, p.criteria);//CV_LKFLOW_INITIAL_GUESSES);
	calcOpticalFlowPyrLK(J, I, to, points, accept[1], err[1], p.winLK, p.level, p.criteria);//CV_LKFLOW_INITIAL_GUESSES | CV_LKFLOW_PYR_A_READY | CV_LKFLOW_PYR_B_READY);

	for (size_t i = 0; i < from.size(); i++)
	{
		accept[0][i] = accept[0][i] && accept[1][i];// both forward and backward can track the points
	}

	NCC(I, J, from, to, accept[0], err[0], p);
	NORM2(from, points, err[1]);

	int goodPts = 0;
	for (size_t i = 0; i < from.size(); i++)
	{
		if (accept[0][i])
		{
			from[goodPts] = from[i];
			to[goodPts] = to[i];
			newWeights[0].push_back(weights[i]);
			//groups[goodPts] = groups[i];
			err[0][goodPts] = err[0][i];
			err[1][goodPts] = err[1][i];
			goodPts++;
		}
	}
	if (goodPts == 0)
	{
		return;
	}
		from.resize(goodPts);
		to.resize(goodPts);

		//groups.resize(goodPts);
		err[0].resize(goodPts);
		err[1].resize(goodPts);
	

	float medNCC = getMedian(&err[0][0], (int)err[0].size());
	float medFB = getMedian(&err[1][0], (int)err[1].size());


	goodPts = 0;

	if (medFB <= p.medFBThreshold)
		for (size_t i = 0; i < from.size(); i++)
		{
			if (err[1][i] <= medFB && err[0][i] >= medNCC)
			{
				from[goodPts] = from[i];
				to[goodPts] = to[i];
				newWeights[1].push_back(newWeights[0][i]);
				//groups[goodPts] = groups[i];
				goodPts++;
			}
		}
	if (goodPts == 0)
	{
		return;
	}
	
		from.resize(goodPts);
     	to.resize(goodPts);
	
	weights = newWeights[1];
	//cout << weights.size()<< endl;
	//groups.resize(goodPts);
}


/*
*  Transform rectangular region B into BNew using the matching points
*  from start to tracked.
*/
void KFlow::transform(Rect_<float> &B,
	Rect_<float> &BNew,
	const vector<Point2f> &start,
	const vector<Point2f> &tracked,
	const KFlowConfigParams &p)
{
	float fDx = 0, fDy = 0;
	int pStart = 0, size = start.size();
	switch (p.transMode)
	{
	case 0: //Median
	{
		vector<float> dx, dy;
		for (int i = pStart; i < (pStart + size); i++)
		{
			dx.push_back(tracked[i].x - start[i].x);
			dy.push_back(tracked[i].y - start[i].y);
		}
		fDx = getMedianUnmanaged(&dx[0], (int)dx.size());
		fDy = getMedianUnmanaged(&dy[0], (int)dy.size());
		break;
	}
	case 1: //Centroid
	{
		Point2f stC, trC;
		for (int i = pStart; i < (pStart + size); i++)
		{
			stC += start[i];
			trC += tracked[i];
		}
		if (size != 0)
		{
			stC.x = stC.x / (float)size;
			stC.y = stC.y / (float)size;
			trC.x = trC.x / (float)size;
			trC.y = trC.y / (float)size;
			fDx = trC.x - stC.x;
			fDy = trC.y - stC.y;
		}
		break;
	}
	default:
		break;
	}


	vector<float> scales;

	for (int i = pStart; i < (pStart + size); i++)
	{
		for (int j = i + 1; j < (pStart + size); j++)
		{
			Point2f diffST = start[i] - start[j];
			Point2f diffTS = tracked[i] - tracked[j];
			float dST = norm(diffST);
			float dTS = norm(diffTS);

			scales.push_back(dTS / dST);
		}
	}

	float fSc = (scales.size() > 0) ?
		getMedianUnmanaged(&scales[0], (int)scales.size()) : 1.f;


	float w = B.width * fSc;
	float h = B.height* fSc;
	BNew = Rect_<float>(B.x - (fSc - 1) * B.width * .5 + fDx,
		B.y - (fSc - 1) * B.height* .5 + fDy,
		w,
		h);
}


/*
*  Transform rectangular region B into BNew using the matching points
*  from start to tracked.
*/
double KFlow::transform(const vector<Point2f> &start,
	const vector<Point2f> &tracked,
	Point2f &shift,
	const KFlowConfigParams &p)
{
	float fDx = 0, fDy = 0;
	int pStart = 0, size = start.size();
	switch (p.transMode)
	{
	case 0: //Median
	{
		vector<float> dx, dy;
		for (int i = pStart; i < (pStart + size); i++)
		{
			dx.push_back(tracked[i].x - start[i].x);
			dy.push_back(tracked[i].y - start[i].y);
		}
		fDx = getMedianUnmanaged(&dx[0], (int)dx.size());
		fDy = getMedianUnmanaged(&dy[0], (int)dy.size());
		break;
	}
	case 1: //Centroid
	{
		Point2f stC, trC;
		for (int i = pStart; i < (pStart + size); i++)
		{
			stC += start[i];
			trC += tracked[i];
		}
		if (size != 0)
		{
			stC.x = stC.x / (float)size;
			stC.y = stC.y / (float)size;
			trC.x = trC.x / (float)size;
			trC.y = trC.y / (float)size;
			fDx = trC.x - stC.x;
			fDy = trC.y - stC.y;
		}
		break;
	}
	default:
		break;
	}


	vector<float> scales;

	for (int i = pStart; i < (pStart + size); i++)
	{
		for (int j = i + 1; j < (pStart + size); j++)
		{
			Point2f diffST = start[i] - start[j];
			Point2f diffTS = tracked[i] - tracked[j];
			float dST = norm(diffST);
			float dTS = norm(diffTS);

			scales.push_back(dTS / dST);
		}
	}

	float fSc = (scales.size() > 0) ?
		getMedianUnmanaged(&scales[0], (int)scales.size()) : 1.f;

	shift = Point2f(fDx, fDy);
	return fSc;
}


/*
*  Returns scale
*/
double KFlow::transform(const vector<Point2f> &start,
	const vector<Point2f> &tracked,
	const vector<float> &weights,
	const KFlowConfigParams &p)
{
	int pStart = 0, size = start.size();

	double weightedSum = 0;
	double sumOfWeights = 0;
	double average = 0;
	double var = 0;
	int count = 0;
	int pos = 0;
	int neg = 0;
	vector<float> scales;
	/*One thing to mention is that we set a new dST&&dTS > 5, this means we don't want to the two points come
	too close. Every calculated point location is not 100% accurate and it has a error based on Gaussian distribution
	Too close means that the random Gaussian distribution error will influence the calculation a lot!*/
	for (int i = pStart; i < (pStart + size); i++)
	{
		float w1 = weights[i];
		for (int j = i + 1; j < (pStart + size); j++)
		{
			float w2 = weights[j];
			Point2f diffST = start[i] - start[j];
			Point2f diffTS = tracked[i] - tracked[j];
			float dST = norm(diffST);
			float dTS = norm(diffTS);
			double ratio = dTS / dST;
			if (dST&&dTS > 5) 
			{
				average += ratio;
				scales.push_back(ratio);
				weightedSum += (w1*w2*(ratio));
				sumOfWeights += w1*w2;
				count++;
			}
		}
	}
	average = average / count;
	for (int i = 0; i < scales.size(); i++)
	{
		var += (scales[i] - average)*(scales[i] - average);
	}
	var = var / count;
	/*We also calculate the variance here. This is important because a high variance means that the object may have
	deformation which our tracker can't handle*/
	if (var > 0.001)
		return 1.0;

	float fSc = (sumOfWeights > 0) ? weightedSum / sumOfWeights : 1.f;
	float fSc2 = (scales.size() > 0) ?
		getMedianUnmanaged(&scales[0], (int)scales.size()) : 1.f;
	
	return (fSc + fSc2) / 2;
}

/* Note that code like if(angle2 - angle1 > CV_PI) is to make sure the code still works well in
border senario. The angle vector of pi-delta and the angle of vector -pi+delta actually very close*/
double KFlow::transformRotation(const vector<Point2f> &start,
	const vector<Point2f> &tracked,
	const Point2f &center,
	const Point2f &shift,
	vector<float> &weights)
{
	vector<float>rotations;
	float rotation = 0;
	float weightsSum = 0;
	float average = 0;
	float var = 0;
	int count = 0;
	for (int i = 0; i < start.size(); i++)
	{
		float w1 = weights[i];
		for (int j = i+1; j < start.size(); j++)
		{
			float x0, y0, x1, y1, scale, angle1, angle2, angle;
			float w2 = weights[j];
			x0 = start[i].x - start[j].x;
			y0 = start[i].y - start[j].y;
			x1 = tracked[i].x - tracked[j].x;
			y1 = tracked[i].y - tracked[j].y;
			if (sqrt(x0*x0 + y0*y0) > 5&& sqrt(x1*x1 + y1*y1) > 5)
			{
				angle1 = arctan2(x0, y0);
				angle2 = arctan2(x1, y1);
				count++;
				if (angle2 - angle1 > CV_PI) 
					angle = angle2 - angle1 - 2 * CV_PI;
				else if (angle2 - angle1 < -CV_PI)
					angle = angle2 - angle1 + 2 * CV_PI;
				else
					angle = angle2 - angle1;
				//cout << angle << " ";
				rotations.push_back(angle);
				average += angle;
				rotation = rotation + w1*w2*angle;
				weightsSum += w1*w2;
			}
		}
	}
	
	average /= count;
  	for (int i = 0; i < count; i++)
	{
		var += (rotations[i] - average)*(rotations[i] - average);
	}
	var /= count;
	if (var > 0.001)
		return 0.0;
	
	
	if (rotations.size() > 0)//if there is no valid point, just keep the current angle
	{
		rotation = rotation / weightsSum;
		float fSc2 =getMedianUnmanaged(&rotations[0],(int)rotations.size()) ;
		rotation += fSc2;
		rotation /= 2;
		
	}
	else
		rotation = 0;
	return rotation;
}

///*
// *  Returns scale
// */
//double KFlow::transform(const vector<Point2f> &start,
//                        const vector<Point2f> &tracked,
//                        const KFlowConfigParams &p)
//{
//    int pStart = 0, size = start.size();
//    
//    vector<float> scales;
//    
//    for (int i = pStart; i < (pStart+ size); i++)
//    {
//        for (int j = i + 1; j < (pStart+ size); j++)
//        {
//            Point2f diffST = start[i] - start[j];
//            Point2f diffTS = tracked[i] - tracked[j];
//            float dST = norm(diffST);
//            float dTS = norm(diffTS);
//            
//            scales.push_back(dTS/dST);
//        }
//    }
//    
//    float fSc = (scales.size() > 0)?
//    getMedianUnmanaged(&scales[0],(int)scales.size()): 1.f;
//
//    return fSc;
//}

/*
* Computes Euclidean distance (NORM2) between two list of points.
*/
void KFlow::NORM2(vector<Point2f> &ptsI,
	vector<Point2f> &ptsJ,
	vector<float> &distances)
{
	for (size_t i = 0; i < ptsI.size(); ++i)
	{
		distances[i] = norm(ptsI[i] - ptsJ[i]);
	}
}


//#define ELEM_SWAP(a,b) { register float t=(a);(a)=(b);(b)=t; }
#define ELEM_SWAP(a,b) { float t=(a);(a)=(b);(b)=t; }

/**
* Returns median of the array. Changes array!
* @param arr the array
* @pram n length of array
*
*  This Quickselect routine is based on the algorithm described in
*  "Numerical recipes in C", Second Edition,
*  Cambridge University Press, 1992, Section 8.5, ISBN 0-521-43108-5
*  This code by Nicolas Devillard - 1998. Public domain.
*/
float KFlow::getMedianUnmanaged(float arr[], int n)
{
	int low, high;
	int median;
	int middle, ll, hh;

	low = 0;
	high = n - 1;
	median = (low + high) / 2;

	for (;;)
	{
		if (high <= low)  /* One element only */
			return arr[median];

		if (high == low + 1)
		{
			/* Two elements only */
			if (arr[low] > arr[high])
				ELEM_SWAP(arr[low], arr[high]);

			return arr[median];
		}

		/* Find median of low, middle and high items; swap into position low */
		middle = (low + high) / 2;

		if (arr[middle] > arr[high])
			ELEM_SWAP(arr[middle], arr[high]);

		if (arr[low] > arr[high])
			ELEM_SWAP(arr[low], arr[high]);

		if (arr[middle] > arr[low])
			ELEM_SWAP(arr[middle], arr[low]);

		/* Swap low item (now in position middle) into position (low+1) */
		ELEM_SWAP(arr[middle], arr[low + 1]);

		/* Nibble from each end towards middle, swapping items when stuck */
		ll = low + 1;
		hh = high;

		for (;;)
		{
			do
				ll++;

			while (arr[low] > arr[ll]);

			do
				hh--;

			while (arr[hh] > arr[low]);

			if (hh < ll)
				break;

			ELEM_SWAP(arr[ll], arr[hh]);
		}

		/* Swap middle item (in position low) back into correct position */
		ELEM_SWAP(arr[low], arr[hh]);

		/* Re-set active partition */
		if (hh <= median)
			low = ll;

		if (hh >= median)
			high = hh - 1;
	}
}

/**
* Calculates Median of the array. Don't change array(makes copy).
* @param arr the array
* @pram n length of array
*/
float KFlow::getMedian(float arr[], int n)
{
	float *temP = (float *)malloc(sizeof(float) * n);
	//  int i;
	//  for (i = 0; i < n; i++)
	//  {
	//    temP[i] = arr[i];
	//  }
	memcpy(temP, arr, sizeof(float) * n);
	float median;
	median = getMedianUnmanaged(temP, n);
	free(temP);
	return median;
}

