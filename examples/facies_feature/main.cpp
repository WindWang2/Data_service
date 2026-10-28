// Example downstream module: prepares curve features for facies inference.
// No SQL, file parser, or UI dependency. This is feature preparation, not a facies classifier.
#include "paleo_data/service.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>
using namespace paleo::data;
struct CurveFeature { long long valid=0,missing=0; double mean=Missing,standardDeviation=Missing,minimum=Missing,maximum=Missing; };
bool prepareFeature(DataService& data,long long wellId,long long curveId,double top,double bottom,CurveFeature& feature,std::string& error){
    feature={};WellFeatureTable table;if(!data.prepareWellFeatures(wellId,{curveId},top,bottom,true,table,error))return false;
    double mean=0,m2=0;for(double value:table.columns[0].values){if(!std::isfinite(value)){++feature.missing;continue;}++feature.valid;const double delta=value-mean;mean+=delta/feature.valid;m2+=delta*(value-mean);if(feature.valid==1)feature.minimum=feature.maximum=value;else{feature.minimum=std::min(feature.minimum,value);feature.maximum=std::max(feature.maximum,value);}}
    if(feature.valid){feature.mean=mean;feature.standardDeviation=std::sqrt(m2/feature.valid);}return true;
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);auto args=app.arguments();if(args.size()<2){std::cerr<<"usage: facies_feature_example <project directory> [well name] [curve name] [top depth] [bottom depth]\n";return 1;}DataService data;std::string error;
    if(!data.openProject(args[1].toUtf8().toStdString(),error)){std::cerr<<error;return 2;}std::vector<WellSummary>wells;if(!data.listWells(wells,error)){std::cerr<<error;return 2;}
    for(auto& well:wells){if(args.size()>2&&well.name!=args[2].toUtf8().toStdString())continue;std::vector<DataEntry> curves;if(!data.listData("curves",well.id,curves,error)){std::cerr<<error;return 2;}for(auto& curve:curves){if(args.size()>3&&curve.name!=args[3].toUtf8().toStdString())continue;double top=Missing,bottom=Missing;bool ok=false;if(args.size()>4){top=args[4].toDouble(&ok);if(!ok)return 3;}if(args.size()>5){bottom=args[5].toDouble(&ok);if(!ok)return 3;}CurveFeature feature;if(!prepareFeature(data,well.id,curve.id,top,bottom,feature,error)){std::cerr<<error;return 2;}std::cout<<"well="<<well.name<<" curve="<<curve.name<<" curveId="<<curve.id<<" valid="<<feature.valid<<" missing="<<feature.missing<<" mean="<<feature.mean<<" stddev="<<feature.standardDeviation<<" min="<<feature.minimum<<" max="<<feature.maximum<<'\n';return 0;}}
    std::cerr<<"No matching curve\n";return 4;
}
