/*-------------------------------------------------------------------------------
This file is part of Ranger.
    
Ranger is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Ranger is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Ranger. If not, see <http://www.gnu.org/licenses/>.

Written by: 

Marvin N. Wright
Institut für Medizinische Biometrie und Statistik
Universität zu Lübeck
Ratzeburger Allee 160
23562 Lübeck 

http://www.imbs-luebeck.de
wright@imbs.uni-luebeck.de
#-------------------------------------------------------------------------------*/

#include <unordered_map>
#include <algorithm>
#include <iterator>
#include <random>
#include <stdexcept>
#include <cmath>
#include <string>

#include "utility.h"
#include "ForestClassification.h"
#include "TreeClassification.h"
#include "Data.h"

ForestClassification::ForestClassification() {
}

ForestClassification::~ForestClassification() {
}

void ForestClassification::loadForest(size_t dependent_varID, size_t num_trees,
    std::vector<std::vector<std::vector<size_t>> >& forest_child_nodeIDs,
    std::vector<std::vector<size_t>>& forest_split_varIDs, std::vector<std::vector<double>>& forest_split_values,
    std::vector<double>& class_values) {

  this->dependent_varID = dependent_varID;
  this->num_trees = num_trees;
  this->class_values = class_values;

  // Create trees
  trees.reserve(num_trees);
  for (size_t i = 0; i < num_trees; ++i) {
    Tree* tree = new TreeClassification(forest_child_nodeIDs[i], forest_split_varIDs[i], forest_split_values[i],
        &class_values, &response_classIDs);
    trees.push_back(tree);
  }

  // Create thread ranges
  equalSplit(thread_ranges, 0, num_trees - 1, num_threads);
}

void ForestClassification::initInternal(std::string status_variable_name) {

  // If mtry not set, use floored square root of number of independent variables.
  if (mtry == 0) {
    int temp = sqrt(num_variables - 1);
    mtry = std::max(1, temp);
  }

  // Set minimal node size
  if (min_node_size == 0) {
    min_node_size = DEFAULT_MIN_NODE_SIZE_CLASSIFICATION;
  }

  // Create class_values and response_classIDs
  if (!prediction_mode) {
    for (size_t i = 0; i < num_samples; ++i) {
      double value = data->get(i, dependent_varID);

      // If classID is already in class_values, use ID. Else create a new one.
      uint classID = find(class_values.begin(), class_values.end(), value) - class_values.begin();
      if (classID == class_values.size()) {
        class_values.push_back(value);
      }
      response_classIDs.push_back(classID);
    }
  }
}

void ForestClassification::growInternal() {
  trees.reserve(num_trees);
  for (size_t i = 0; i < num_trees; ++i) {
    trees.push_back(new TreeClassification(&class_values, &response_classIDs));
  }
}

void ForestClassification::predictInternal() {

  // First dim trees, second dim samples
  size_t num_prediction_samples = trees[0]->getPredictions()[0].size();
  predictions.reserve(num_prediction_samples);

  // For all samples take majority vote over all trees
  for (size_t sample_idx = 0; sample_idx < num_prediction_samples; ++sample_idx) {

    // Count classes over trees and save class with maximum count
    std::unordered_map<double, size_t> class_count;
    for (size_t tree_idx = 0; tree_idx < num_trees; ++tree_idx) {
      double value = trees[tree_idx]->getPredictions()[0][sample_idx];
      ++class_count[value];
    }

    std::vector<double> temp;
    temp.push_back(mostFrequentValue(class_count, random_number_generator));
    predictions.push_back(temp);
  }
}

void ForestClassification::computePredictionErrorInternal() {

  // Class counts for samples
  std::vector<std::unordered_map<double, size_t>> class_counts;
  class_counts.reserve(num_samples);
  for (size_t i = 0; i < num_samples; ++i) {
    class_counts.push_back(std::unordered_map<double, size_t>());
  }

  // For each tree loop over OOB samples and count classes
  for (size_t tree_idx = 0; tree_idx < num_trees; ++tree_idx) {
    for (size_t sample_idx = 0; sample_idx < trees[tree_idx]->getNumSamplesOob(); ++sample_idx) {
      size_t sampleID = trees[tree_idx]->getOobSampleIDs()[sample_idx];
      double value = trees[tree_idx]->getPredictions()[0][sample_idx];
      ++class_counts[sampleID][value];
    }
  }

  // Compute majority vote for each sample
  predictions.reserve(num_samples);
  for (size_t i = 0; i < num_samples; ++i) {
    std::vector<double> temp;
    if (!class_counts[i].empty()) {
      temp.push_back(mostFrequentValue(class_counts[i], random_number_generator));
    } else {
      temp.push_back(NAN);
    }
    predictions.push_back(temp);
  }

  // Compare predictions with true data
  size_t num_missclassifications = 0;
  for (size_t i = 0; i < predictions.size(); ++i) {
    double predicted_value = predictions[i][0];
    if (!isnan(predicted_value)) {
      double real_value = data->get(i, dependent_varID);
      if (predicted_value != real_value) {
        ++num_missclassifications;
      }
      ++classification_table[std::make_pair(real_value, predicted_value)];
    }
  }
  overall_prediction_error = (double) num_missclassifications / (double) predictions.size();
}

void ForestClassification::writeOutputInternal() {
  *verbose_out << "Tree type:                         " << "Classification" << std::endl;
}

void ForestClassification::writeConfusionFile() {

  // Open confusion file for writing
  std::string filename = output_prefix + ".confusion";
  std::ofstream outfile;
  outfile.open(filename, std::ios::out);
  if (!outfile.good()) {
    throw std::runtime_error("Could not write to confusion file: " + filename + ".");
  }

  // Write confusion to file
  outfile << "Overall OOB prediction error (Fraction missclassified): " << overall_prediction_error << std::endl;
  outfile << std::endl;
  outfile << "Class specific prediction errors:" << std::endl;
  outfile << "           ";
  for (auto& class_value : class_values) {
    outfile << "     " << class_value;
  }
  outfile << std::endl;
  for (auto& predicted_value : class_values) {
    outfile << "predicted " << predicted_value << "     ";
    for (auto& real_value : class_values) {
      size_t value = classification_table[std::make_pair(real_value, predicted_value)];
      outfile << value;
      if (value < 10) {
        outfile << "     ";
      } else if (value < 100) {
        outfile << "    ";
      } else if (value < 1000) {
        outfile << "   ";
      } else if (value < 10000) {
        outfile << "  ";
      } else if (value < 100000) {
        outfile << " ";
      }
    }
    outfile << std::endl;
  }

  outfile.close();
  *verbose_out << "Saved confusion matrix to file " << filename << "." << std::endl;
}

void ForestClassification::writePredictionFile() {

  // Open prediction file for writing
  std::string filename = output_prefix + ".prediction";
  std::ofstream outfile;
  outfile.open(filename, std::ios::out);
  if (!outfile.good()) {
    throw std::runtime_error("Could not write to prediction file: " + filename + ".");
  }

  // Write
  outfile << "Predictions: " << std::endl;
  for (size_t i = 0; i < predictions.size(); ++i) {
    for (size_t j = 0; j < predictions[i].size(); ++j) {
      outfile << predictions[i][j] << std::endl;
    }
  }

  *verbose_out << "Saved predictions to file " << filename << "." << std::endl;
}

void ForestClassification::saveToFileInternal(std::ofstream& outfile) {

  // Write num_variables
  outfile.write((char*) &num_variables, sizeof(num_variables));

  // Write treetype
  TreeType treetype = TREE_CLASSIFICATION;
  outfile.write((char*) &treetype, sizeof(treetype));

  // Write class_values
  saveVector1D(class_values, outfile);
}

void ForestClassification::loadFromFileInternal(std::ifstream& infile) {

  // Read number of variables
  size_t num_variables_saved;
  infile.read((char*) &num_variables_saved, sizeof(num_variables_saved));

  // Read treetype
  TreeType treetype;
  infile.read((char*) &treetype, sizeof(treetype));
  if (treetype != TREE_CLASSIFICATION) {
    throw std::runtime_error("Wrong treetype. Loaded file is not a classification forest.");
  }

  // Read class_values
  readVector1D(class_values, infile);

  for (size_t i = 0; i < num_trees; ++i) {

    // Read data
    std::vector<std::vector<size_t>> child_nodeIDs;
    readVector2D(child_nodeIDs, infile);
    std::vector<size_t> split_varIDs;
    readVector1D(split_varIDs, infile);
    std::vector<double> split_values;
    readVector1D(split_values, infile);

    // If dependent variable not in test data, change variable IDs accordingly
    if (num_variables_saved > num_variables) {
      for (auto& varID : split_varIDs) {
        if (varID >= dependent_varID) {
          --varID;
        }
      }
    }

    // Create tree
    Tree* tree = new TreeClassification(child_nodeIDs, split_varIDs, split_values, &class_values, &response_classIDs);
    trees.push_back(tree);
  }
}

