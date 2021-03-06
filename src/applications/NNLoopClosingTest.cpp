//
//
// GFLIP - Geometrical FLIRT Phrases for Large Scale Place Recognition
// Copyright (C) 2012-2013 Gian Diego Tipaldi and Luciano Spinello and Wolfram
// Burgard
//
// This file is part of GFLIP.
//
// GFLIP is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// GFLIP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with GFLIP.  If not, see <http://www.gnu.org/licenses/>.
//

#include <feature/Detector.h>
#include <feature/ShapeContext.h>
#include <feature/BetaGrid.h>
#include <feature/RangeDetector.h>
#include <feature/CurvatureDetector.h>
#include <feature/NormalBlobDetector.h>
#include <feature/NormalEdgeDetector.h>
#include <feature/RansacFeatureSetMatcher.h>
#include <feature/RansacMultiFeatureSetMatcher.h>
#include <sensorstream/CarmenLog.h>
#include <sensorstream/LogSensorStream.h>
#include <sensorstream/SensorStream.h>
#include <utils/SimpleMinMaxPeakFinder.h>
#include <utils/HistogramDistances.h>

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/vector.hpp>

#include <iostream>
#include <string>
#include <string.h>
#include <sstream>
#include <utility>

#include <sys/time.h>

LogSensorStream m_sensorReference(NULL,NULL);

CurvatureDetector *m_detectorCurvature = NULL;
NormalBlobDetector *m_detectorNormalBlob = NULL;
NormalEdgeDetector *m_detectorNormalEdge = NULL;
RangeDetector *m_detectorRange = NULL;
Detector* m_detector = NULL;

BetaGridGenerator *m_betaGenerator = NULL;
ShapeContextGenerator *m_shapeGenerator = NULL;
DescriptorGenerator *m_descriptor = NULL;

RansacFeatureSetMatcher *m_ransac = NULL;

double angErrorTh = 0.2;
double linErrorTh = 0.5;

std::vector< std::vector<InterestPoint *> > m_pointsReference;
// std::vector< BagOfWords > m_scansReference;
std::vector< OrientedPoint2D > m_posesReference;

unsigned int corresp[] = {0, 3, 5, 7, 9, 11, 13, 15};

double m_error[8] = {0.}, m_errorC[8] = {0.}, m_errorR[8] = {0.};
unsigned int m_match[8] = {0}, m_matchC[8] = {0}, m_matchR[8] = {0};
unsigned int m_valid[8] = {0};
unsigned int m_exist[8] = {0};

struct timeval detectTime, describeTime, ransacTime, vocabularyTime;

unsigned int m_localSkip = 1, m_neighborood = 50;

void help(){
    std::cerr << "Geometrical FLIRT Phrases for Large Scale Place Recognition in 2D Range Data" << std::endl
    << "G. D. Tipaldi, L. Spinello, W. Burgard -- Int. Conf. Robotics and Automation (ICRA) 2013" << std::endl
    << std::endl
    << "Usage: ransacLoopClosureTest -filename <logfile> [options] " << std::endl
    << "Options:" << std::endl
    << " -filename          \t The logfile in CARMEN format to process (mandatory)." << std::endl
    << " -nnfile            \t The neighborood file to use (default=<logfile>.nn)." << std::endl
    << " -neighborood       \t The number of neighbors to perform ransac match (default=50)." << std::endl
    << " -scale             \t The number of scales to consider (default=5)." << std::endl
    << " -dmst              \t The number of spanning tree for the curvature detector (deafult=2)." << std::endl
    << " -window            \t The size of the local window for estimating the normal signal (default=3)." << std::endl
    << " -detector          \t The type of detector to use. Available options are (default=0):" << std::endl
    << "                    \t     0 - Curvature detector;" << std::endl
    << "                    \t     1 - Normal edge detector;" << std::endl
    << "                    \t     2 - Normal blob detector;" << std::endl
    << "                    \t     3 - Range detector." << std::endl
    << " -descriptor        \t The type of descriptor to use. Available options are (default=0):" << std::endl
    << "                    \t     0 - Beta - grid;" << std::endl
    << "                    \t     1 - Shape context." << std::endl
    << " -distance          \t The distance function to compare the descriptors. Available options are (default=2):" << std::endl
    << "                    \t     0 - Euclidean distance;" << std::endl
    << "                    \t     1 - Chi square distance;" << std::endl
    << "                    \t     2 - Symmetric Chi square distance;" << std::endl
    << "                    \t     3 - Bhattacharyya distance;" << std::endl
    << "                    \t     4 - Kullback Leibler divergence;" << std::endl
    << "                    \t     5 - Jensen Shannon divergence." << std::endl
    << " -baseSigma         \t The initial standard deviation for the smoothing operator (default=0.2)." << std::endl
    << " -sigmaStep         \t The incremental step for the scales of the smoothing operator. (default=1.4)." << std::endl
    << " -minPeak           \t The minimum value for a peak to be detected (default=0.34)." << std::endl
    << " -minPeakDistance   \t The minimum difference with the neighbors for a peak to be detected (default=0.001)." << std::endl
    << " -acceptanceSigma   \t The standard deviation of the detection error (default=0.1)." << std::endl
    << " -success           \t The success probability of RANSAC (default=0.95)." << std::endl
    << " -inlier            \t The inlier probability of RANSAC (default=0.4)." << std::endl
    << " -matchingThreshold \t The threshold two descriptors are considered matched (default=0.4)." << std::endl
    << " -strategy          \t The strategy for matching two descriptors. Available options are (default=0):" << std::endl
    << "                    \t     0 - Nearest Neighbour. Only the closest match above the threshold is considered;" << std::endl
    << "                    \t     1 - Threshold. All the matches above the threshold are considered." << std::endl
    << std::endl
    << "The program computes matches every scan with all the others in the logfile. To work properly, the logfile must"  << std::endl
    << "be corrected by a SLAM algorithm beforehand (the provided logfiles are already corrected). The program writes" << std::endl
    << "the following matching statistics in the following files:" << std::endl
    << "    correct matches: <logfile>_<detector>_<descriptor>_<distance>_NN<nn>_match.dat" << std::endl
    << "    matching error : <logfile>_<detector>_<descriptor>_<distance>_NN<nn>_match.dat" << std::endl
    << "    matching time  : <logfile>_<detector>_<descriptor>_<distance>_NN<nn>_match.dat" << std::endl
    << std::endl
    << std::endl;
}


bool pairComparator ( const std::pair<double, unsigned int>& l, const std::pair<double, unsigned int>& r)
   { return l.first > r.first; }


void match(unsigned int position, const std::vector<unsigned int>& NN)
{
    m_sensorReference.seek(position);
    
    std::vector<InterestPoint *> pointsLocal(m_pointsReference[position].size());
    const LaserReading* lreadReference = dynamic_cast<const LaserReading*>(m_sensorReference.current());
    for(unsigned int j = 0; j < m_pointsReference[position].size(); j++){
	InterestPoint * point = new InterestPoint(*m_pointsReference[position][j]);
	point->setPosition(lreadReference->getLaserPose().ominus(point->getPosition()));
	pointsLocal[j] = point;
    }
       
    unsigned int inliers[m_pointsReference.size()];
    double results[m_pointsReference.size()];
    double linearErrors[m_pointsReference.size()];
    double angularErrors[m_pointsReference.size()];
    struct timeval start, end, diff, sum;
    
    bool existing = false;

//     gettimeofday(&start,NULL);
//     std::vector<std::pair<double, unsigned int> > NN(m_scansReference.size());
//     std::multimap<double, unsigned int> NN;
    for(unsigned int i = 0; i < m_posesReference.size(); i++) {
			results[i] = 1e17;
			inliers[i] = 0;
			linearErrors[i] = 1e17;
			angularErrors[i] = 1e17;
			if(fabs(double(i) - double(position)) < m_localSkip) {
					results[i] = 1e17;
					inliers[i] = 0;
					linearErrors[i] = 1e17;
					angularErrors[i] = 1e17;
					continue;
			}
// 			double sim = m_scansReference[position].sim(m_scansReference[i]);
// 			NN[i] = std::make_pair(sim, i);
// 			NN.insert(std::make_pair(sim, i));
			OrientedPoint2D deltaGT = m_posesReference[position] - m_posesReference[i]; 
			existing = existing || ((deltaGT * deltaGT ) < (2 * linErrorTh * linErrorTh) && (deltaGT.theta * deltaGT.theta) < (2 * angErrorTh * angErrorTh));
    }
//     if(m_neighborood < NN.size()) std::partial_sort(NN.begin(), NN.begin() + m_neighborood, NN.end(), pairComparator);
//     gettimeofday(&end,NULL);
//     timersub(&end, &start, &diff);
//     timeradd(&vocabularyTime, &diff, &sum);
//     vocabularyTime = sum;
    
//     std::multimap<double, unsigned int>::const_reverse_iterator it = NN.rbegin();
    std::vector<unsigned int>::const_iterator it = NN.begin();
    for(unsigned int i = 0; i < m_neighborood && it != NN.end(); i++, it++){
      OrientedPoint2D transform;
      std::vector< std::pair<InterestPoint*, InterestPoint* > > correspondences;
      gettimeofday(&start,NULL);
// 	std::cout << m_pointsReference[i].size() << " vs. " << pointsLocal.size() << std::endl;
      results[*it] = m_ransac->matchSets(m_pointsReference[*it], pointsLocal, transform, correspondences);
      gettimeofday(&end,NULL);
      timersub(&end, &start, &diff);
      timeradd(&ransacTime, &diff, &sum);
      ransacTime = sum;
      inliers[*it] = correspondences.size();
      OrientedPoint2D delta = m_posesReference[position] - transform; 
      linearErrors[*it] = correspondences.size() ? delta * delta : 1e17;
      angularErrors[*it] = correspondences.size() ? delta.theta * delta.theta : 1e17;
    }
    
    for(unsigned int c = 0; c < 8; c++){
	unsigned int maxCorres = 0;
	double maxResult = 1e17;
	double linError = 1e17, angError = 1e17;
	double linErrorC = 1e17, angErrorC = 1e17;
	double linErrorR = 1e17, angErrorR = 1e17;
	bool valid = false;
	unsigned int maxIndex = 0;
        for(unsigned int i = 0; i < m_pointsReference.size(); i++){
	    if(fabs(double(i) - double(position)) < m_localSkip) {
		continue;
	    }
	    if(linError + angError > linearErrors[i] + angularErrors[i]) {
		linError = linearErrors[i];
		angError = angularErrors[i];
	    }
	    if(maxCorres < inliers[i]){
		linErrorC = linearErrors[i];
		angErrorC = angularErrors[i];
		maxCorres = inliers[i];
	    }
	    if(maxResult > results[i]){
		linErrorR = linearErrors[i];
		angErrorR = angularErrors[i];
		maxResult = results[i];
		maxIndex = i;
	    }
	    valid = valid || inliers[i] >= corresp[c];
	}
	
	bool found = false;
	if(valid){
	    found = (linError <= (linErrorTh * linErrorTh) && angError <= (angErrorTh * angErrorTh) );
	    m_match[c] += (linError <= (linErrorTh * linErrorTh) && angError <= (angErrorTh * angErrorTh) );
	    m_matchC[c] += (linErrorC <= (linErrorTh * linErrorTh) && angErrorC <= (angErrorTh * angErrorTh) );
	    m_matchR[c] += (linErrorR <= (linErrorTh * linErrorTh) && angErrorR <= (angErrorTh * angErrorTh) );
	    
	    m_error[c] += sqrt(linError + angError);
	    m_errorC[c] += sqrt(linErrorC + angErrorC);
	    m_errorR[c] += sqrt(linErrorR + angErrorR);
	    
	    m_valid[c]++;
	}
	m_exist[c] += existing || found;
    }
	

}

void writePoses(){
    unsigned int i = 0;
    unsigned int position = m_sensorReference.tell();
    m_sensorReference.seek(0,END);
    unsigned int last = m_sensorReference.tell();
    m_sensorReference.seek(0);
    
    std::string bar(50, ' ');
    bar[0] = '#';
    unsigned int progress = 0;
    
    while(!m_sensorReference.end()){
	unsigned int currentProgress = (m_sensorReference.tell()*100)/last;
	if (progress < currentProgress){
	    progress = currentProgress;
	    bar[progress/2] = '#';
	    std::cout << "\rWriting poses     [" << bar << "] " << (m_sensorReference.tell()*100)/last << "%" << std::flush;
	}
	const LaserReading* lreadReference = dynamic_cast<const LaserReading*>(m_sensorReference.next());
	if (lreadReference){
	    m_posesReference[i] = lreadReference->getLaserPose();
	    i++;
	}
    }
    m_sensorReference.seek(position);
    std::cout << " done." << std::endl;
}

void detectLog(){
    unsigned int i = 0;
    unsigned int position = m_sensorReference.tell();
    m_sensorReference.seek(0,END);
    unsigned int last = m_sensorReference.tell();
    m_sensorReference.seek(0);
    
    std::string bar(50, ' ');
    bar[0] = '#';
    unsigned int progress = 0;
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    while(!m_sensorReference.end()){
	unsigned int currentProgress = (m_sensorReference.tell()*100)/last;
	if (progress < currentProgress){
	    progress = currentProgress;
	    bar[progress/2] = '#';
	    std::cout << "\rDetecting points  [" << bar << "] " << (m_sensorReference.tell()*100)/last << "%" << std::flush;
	}
	const LaserReading* lreadReference = dynamic_cast<const LaserReading*>(m_sensorReference.next());
	if (lreadReference){
	    m_detector->detect(*lreadReference, m_pointsReference[i]);
	    m_posesReference[i] = lreadReference->getLaserPose();
	    i++;
	}
    }
    gettimeofday(&end,NULL);
    timersub(&end,&start,&detectTime);
    m_sensorReference.seek(position);
    std::cout << " done." << std::endl;
}

void countLog(){
    double flirtNum = 0.;
    uint count = 0;
    
    std::string bar(50, ' ');
    bar[0] = '#';

    unsigned int progress = 0;
    for(unsigned int i = 0; i < m_pointsReference.size(); i++){
	unsigned int currentProgress = (i*100)/(m_pointsReference.size() - 1);
	if (progress < currentProgress){
	    progress = currentProgress;
	    bar[progress/2] = '#';
	    std::cout << "\rCounting points  [" << bar << "] " << progress << "%" << std::flush;
	}
	if(m_pointsReference[i].size()){
	    flirtNum += m_pointsReference[i].size();
	    count++;
	}
    }
    flirtNum=count?flirtNum/double(count):0.;
    std::cout << " done.\nFound " << flirtNum << " FLIRT features per scan." << std::endl;
}

void describeLog(){
    unsigned int i = 0;
    unsigned int position = m_sensorReference.tell();
    m_sensorReference.seek(0,END);
    unsigned int last = m_sensorReference.tell();
    m_sensorReference.seek(0);

    std::string bar(50, ' ');
    bar[0] = '#';
    struct timeval start, end;
    gettimeofday(&start, NULL);
    unsigned int progress = 0;
    
    while(!m_sensorReference.end()){
	unsigned int currentProgress = (m_sensorReference.tell()*100)/last;
	if (progress < currentProgress){
	    progress = currentProgress;
	    bar[progress/2] = '#';
	    std::cout << "\rDescribing points  [" << bar << "] " << progress << "%" << std::flush;
	}
	const LaserReading* lreadReference = dynamic_cast<const LaserReading*>(m_sensorReference.next());
	if (lreadReference){
	    for(unsigned int j = 0; j < m_pointsReference[i].size(); j++){
		m_pointsReference[i][j]->setDescriptor(m_descriptor->describe(*m_pointsReference[i][j], *lreadReference));
	    }
	    i++;
	}
    }
    gettimeofday(&end,NULL);
    timersub(&end,&start,&describeTime);
    m_sensorReference.seek(position);
    std::cout << " done." << std::endl;
}

void readNNTable(std::vector<std::vector<unsigned int> >&  NNTable, std::ifstream& nnstream){
  unsigned int max_line = 65536;
  char buffer[max_line];

    while(nnstream){
	nnstream.getline(buffer,max_line);
	std::istringstream instream(buffer);
	unsigned int nn;
	double tmp;
	instream >> nn >> tmp >> tmp;
	std::vector<unsigned int> NNList(nn);
	for (unsigned int i = 0; i < nn; i++) {
	  instream >> NNList[i];
	}
	NNTable.push_back(NNList);
    }
}

void describeScans(HistogramDistance<double>* distance){
    std::string bar(50, ' ');
    bar[0] = '#';

    unsigned int progress = 0;
    for(unsigned int i = 0; i < m_pointsReference.size(); i++){
	unsigned int currentProgress = (i*100)/(m_pointsReference.size() - 1);
	if (progress < currentProgress){
	    progress = currentProgress;
	    bar[progress/2] = '#';
	    std::cout << "\rDescribing scans  [" << bar << "] " << progress << "%" << std::flush;
	}
	for(unsigned int j = 0; j < m_pointsReference[i].size(); j++){
	    InterestPoint * point = m_pointsReference[i][j];
	    if(ShapeContext *shape = dynamic_cast<ShapeContext*>(point->getDescriptor())){
		shape->setDistanceFunction(distance);
	    } else if(BetaGrid *beta = dynamic_cast<BetaGrid*>(point->getDescriptor())){
		beta->setDistanceFunction(distance);
	    }
	}
    }
    std::cout << " done." << std::endl;
}


int main(int argc, char **argv){

    std::string filename(""), nnfile("");
    unsigned int scale = 5, dmst = 2, window = 3, detectorType = 0, descriptorType = 0, distanceType = 2, strategy = 0;
    double baseSigma = 0.2, sigmaStep = 1.4, minPeak = 0.34, minPeakDistance = 0.001, acceptanceSigma = 0.1, success = 0.95, inlier = 0.4, matchingThreshold = 0.4;
    bool useMaxRange = false;
    
    int i = 1;
    while(i < argc){
		if(strncmp("-filename", argv[i], sizeof("-filename")) == 0 ){
			filename = argv[++i];
			i++;
		} else if(strncmp("-nnfile", argv[i], sizeof("-nnfile")) == 0 ){
			nnfile = argv[++i];
			i++;
		} else if(strncmp("-detector", argv[i], sizeof("-detector")) == 0 ){
			detectorType = atoi(argv[++i]);
			i++;
		} else if(strncmp("-descriptor", argv[i], sizeof("-descriptor")) == 0 ){
			descriptorType = atoi(argv[++i]);
			i++;
		} else if(strncmp("-distance", argv[i], sizeof("-distance")) == 0 ){
			distanceType = atoi(argv[++i]);
			i++;
		} else if(strncmp("-strategy", argv[i], sizeof("-strategy")) == 0 ){
			strategy = atoi(argv[++i]);
			i++;
		} else if(strncmp("-baseSigma", argv[i], sizeof("-baseSigma")) == 0 ){
			baseSigma = strtod(argv[++i], NULL);
			i++;
		} else if(strncmp("-sigmaStep", argv[i], sizeof("-sigmaStep")) == 0 ){
			sigmaStep = strtod(argv[++i], NULL);
			i++;
		} else if(strncmp("-minPeak", argv[i], sizeof("-minPeak")) == 0 ){
			minPeak = strtod(argv[++i], NULL);
			i++;
		} else if(strncmp("-minPeakDistance", argv[i], sizeof("-minPeakDistance")) == 0 ){
			minPeakDistance = strtod(argv[++i], NULL);
			i++;
		} else if(strncmp("-scale", argv[i], sizeof("-scale")) == 0 ){
			scale = atoi(argv[++i]);
			i++;
		} else if(strncmp("-dmst", argv[i], sizeof("-dmst")) == 0 ){
			dmst = atoi(argv[++i]);
			i++;
		} else if(strncmp("-window", argv[i], sizeof("-window")) == 0 ){
			window = atoi(argv[++i]);
			i++;
		} else if(strncmp("-acceptanceSigma", argv[i], sizeof("-acceptanceSigma")) == 0 ){
			acceptanceSigma = strtod(argv[++i], NULL);
			i++;
		} else if(strncmp("-success", argv[i], sizeof("-success")) == 0 ){
			success = strtod(argv[++i], NULL);
			i++;
		} else if(strncmp("-inlier", argv[i], sizeof("-inlier")) == 0 ){
			inlier = strtod(argv[++i], NULL);
			i++;
		} else if(strncmp("-matchingThreshold", argv[i], sizeof("-matchingThreshold")) == 0 ){
			matchingThreshold = strtod(argv[++i], NULL);
			i++;
		} else if(strncmp("-localSkip", argv[i], sizeof("-localSkip")) == 0 ){
			m_localSkip = atoi(argv[++i]);
			i++;
		} else if(strncmp("-neighborood", argv[i], sizeof("-neighborood")) == 0 ){
			m_neighborood = atoi(argv[++i]);
			i++;
		} else if(strncmp("-help", argv[i], sizeof("-localSkip")) == 0 ){
			help();
			exit(0);
		} else {
			i++;
		}
    }
    
    if(!filename.size()){
		help();
		exit(-1);
    }
    if(!nnfile.size()){
      nnfile = filename.substr(0,filename.find_last_of('.')).append(".nn");
    }
    

    
    
    CarmenLogWriter writer;
    CarmenLogReader reader;
    
    m_sensorReference = LogSensorStream(&reader, &writer);
    
    m_sensorReference.load(filename);
    
    SimpleMinMaxPeakFinder *m_peakMinMax = new SimpleMinMaxPeakFinder(minPeak, minPeakDistance);
    
    
    std::string detector("");
    switch(detectorType){
	case 0:
	    m_detectorCurvature = new CurvatureDetector(m_peakMinMax, scale, baseSigma, sigmaStep, dmst);
	    m_detectorCurvature->setUseMaxRange(useMaxRange);
	    m_detector = m_detectorCurvature;
	    detector = "curvature";
	    break;
	case 1:
	    m_detectorNormalEdge = new NormalEdgeDetector(m_peakMinMax, scale, baseSigma, sigmaStep, window);
	    m_detectorNormalEdge->setUseMaxRange(useMaxRange);
	    m_detector = m_detectorNormalEdge;
	    detector = "edge";
	    break;
	case 2:
	    m_detectorNormalBlob = new NormalBlobDetector(m_peakMinMax, scale, baseSigma, sigmaStep, window);
	    m_detectorNormalBlob->setUseMaxRange(useMaxRange);
	    m_detector = m_detectorNormalBlob;
	    detector = "blob";
	    break;
	case 3:
	    m_detectorRange = new RangeDetector(m_peakMinMax, scale, baseSigma, sigmaStep);
	    m_detectorRange->setUseMaxRange(useMaxRange);
	    m_detector = m_detectorRange;
	    detector = "range";
	    break;
	default:
	    std::cerr << "Wrong detector type" << std::endl;
	    exit(-1);
    }
    
    HistogramDistance<double> *dist = NULL;
    
    std::string distance("");
    switch(distanceType){
	case 0:
	    dist = new EuclideanDistance<double>();
	    distance = "euclid";
	    break;
	case 1:
	    dist = new Chi2Distance<double>();
	    distance = "chi2";
	    break;
	case 2:
	    dist = new SymmetricChi2Distance<double>();
	    distance = "symchi2";
	    break;
	case 3:
	    dist = new BatthacharyyaDistance<double>();
	    distance = "batt";
	    break;
	case 4:
	    dist = new KullbackLeiblerDistance<double>();
	    distance = "kld";
	    break;
	case 5:
	    dist = new JensenShannonDistance<double>();
	    distance = "jsd";
	    break;
	default:
	    std::cerr << "Wrong distance type" << std::endl;
	    exit(-1);
    }
    
    std::string descriptor("");
    switch(descriptorType){
	case 0:
	    m_betaGenerator = new BetaGridGenerator(0.02, 0.5, 4, 12);
	    m_betaGenerator->setDistanceFunction(dist);
	    m_descriptor = m_betaGenerator;
	    descriptor = "beta";
	    break;
	case 1:
	    m_shapeGenerator = new ShapeContextGenerator(0.02, 0.5, 4, 12);
	    m_shapeGenerator->setDistanceFunction(dist);
	    m_descriptor = m_shapeGenerator;
	    descriptor = "shape";
	    break;
	default:
	    std::cerr << "Wrong descriptor type" << std::endl;
	    exit(-1);
    }
    
    switch(strategy){
	case 0:
	    m_ransac = new RansacFeatureSetMatcher(acceptanceSigma * acceptanceSigma * 5.99, success, inlier, matchingThreshold, acceptanceSigma * acceptanceSigma * 3.84, false);
	    break;
	case 1:
	    m_ransac = new RansacMultiFeatureSetMatcher(acceptanceSigma * acceptanceSigma * 5.99, success, inlier, matchingThreshold, acceptanceSigma * acceptanceSigma * 3.84, false);
	    break;
	default:
	    std::cerr << "Wrong strategy type" << std::endl;
	    exit(-1);
    }
    
    std::cerr << "Processing file:\t" << filename << 
		 "\nDetector:\t\t" << detector << 
		 "\nDescriptor:\t\t" << descriptor << 
		 "\nDistance:\t\t" << distance << 
		 "\nLocal Skip:\t\t" << m_localSkip <<
		 "\nNN file:\t\t" << nnfile << std::endl;
    
    m_sensorReference.seek(0,END);
    unsigned int end = m_sensorReference.tell();
    m_sensorReference.seek(0,BEGIN);

    std::vector< std::vector<unsigned int> > NNTable;
    NNTable.reserve(end+1);
    std::ifstream nnstream(nnfile);
    readNNTable(NNTable, nnstream);
		 
    m_pointsReference.resize(end + 1);
    m_posesReference.resize(end + 1);
    
    writePoses();
    try {
      std::string featureFile = filename.substr(0,filename.find_last_of('.')) + ".flt";
      std::ifstream featureStream(featureFile.c_str());
      boost::archive::binary_iarchive featureArchive(featureStream);
      std::cout << "Loading feature file " << featureFile << " ...";
      featureArchive >> m_pointsReference;
      std::cout << " done." << std::endl;
      describeScans(dist);
    } catch(boost::archive::archive_exception& exc) {
      detectLog();
      countLog();
      describeLog();
    }
    
    std::string outfile = filename.substr(0,filename.find_last_of('.'));
    
    timerclear(&ransacTime);
    
    std::string bar(50,' ');
    bar[0] = '#';
    unsigned int progress = 0;
    
    for(unsigned int i =0; i < m_pointsReference.size(); i++){
	unsigned int currentProgress = (i*100)/(m_pointsReference.size() - 1);
	if (progress < currentProgress){
	    progress = currentProgress;
	    bar[progress/2] = '#';
	    std::cout << "\rMatching  [" << bar << "] " << progress << "%" << std::flush;
	}
    	match(i, NNTable[i]);
    }
    std::cout << " done." << std::endl;
    
    std::stringstream matchFile;
    std::stringstream errorFile;
    std::stringstream timeFile;

    matchFile << outfile << "_" << detector << "_" << descriptor << "_" << distance << "_NN" << std::setw(4) << std::setfill('0') << m_neighborood << "_match.dat";
    errorFile << outfile << "_" << detector << "_" << descriptor << "_" << distance << "_NN" << std::setw(4) << std::setfill('0') << m_neighborood << "_error.dat";
    timeFile << outfile << "_" << detector << "_" << descriptor << "_" << distance << "_NN" << std::setw(4) << std::setfill('0') << m_neighborood << "_time.dat";

    std::ofstream matchOut(matchFile.str().c_str());
    std::ofstream errorOut(errorFile.str().c_str());
    std::ofstream timeOut(timeFile.str().c_str());
    
    
    matchOut << "# Number of matches according to various strategies" << std::endl;
    matchOut << "# The valid matches are the one with at least n correspondences in the inlier set " << std::endl;
    matchOut << "# where n = {0, 3, 5, 7, 9, 11, 13, 15}, one for each line " << std::endl;
    matchOut << "# optimal \t correspondence \t residual \t valid \t existing" << std::endl;
    
    errorOut << "# Mean error according to various strategies" << std::endl;
    errorOut << "# The valid matches are the one with at least n correspondences in the inlier set " << std::endl;
    errorOut << "# where n = {0, 3, 5, 7, 9, 11, 13, 15}, one for each line " << std::endl;
    errorOut << "# optimal \t correspondence \t residual \t valid \t existing" << std::endl;
	
    timeOut << "# Total time spent for the various steps" << std::endl;
    timeOut << "# detection \t description \t RANSAC \t Vocabulary" << std::endl;

    for(unsigned int c = 0; c < 8; c++){
// 	m_exist[c] = m_pointsReference.size();
      matchOut << m_match[c] << "\t" << m_matchC[c] << "\t" << m_matchR[c] << "\t" << m_valid[c] << "\t" << m_exist[c] << std::endl;
      errorOut << m_error[c]/m_valid[c] << "\t" << m_errorC[c]/m_valid[c] << "\t" << m_errorR[c]/m_valid[c] << "\t" << m_valid[c] << "\t" << m_exist[c] << std::endl;
    }
    timeOut << double(detectTime.tv_sec) + 1e-06 * double(detectTime.tv_usec) << "\t"
	    << double(describeTime.tv_sec) + 1e-06 * double(describeTime.tv_usec) << "\t"
	    << double(ransacTime.tv_sec) + 1e-06 * double(ransacTime.tv_usec) << "\t"
	    << double(vocabularyTime.tv_sec) + 1e-06 * double(vocabularyTime.tv_usec) << std::endl;
}

