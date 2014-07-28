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

#include <unordered_set>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <vector>

#include "TreeClassification.h"
#include "utility.h"
#include "Data.h"

TreeClassification::TreeClassification(std::vector<double>* class_values, std::vector<uint>* response_classIDs) :
    class_values(class_values), response_classIDs(response_classIDs) {
}

TreeClassification::TreeClassification(std::vector<std::vector<size_t>>& child_nodeIDs,
    std::vector<size_t>& split_varIDs, std::vector<double>& split_values, std::vector<double>* class_values,
    std::vector<uint>* response_classIDs) :
    Tree(child_nodeIDs, split_varIDs, split_values), class_values(class_values), response_classIDs(response_classIDs) {
}

TreeClassification::~TreeClassification() {
}

void TreeClassification::addPrediction(size_t nodeID, size_t sampleID) {
  predictions[0][sampleID] = split_values[nodeID];
}

double TreeClassification::estimate(size_t nodeID) {

  // Count classes over samples in node and return class with maximum count
  std::unordered_map<double, size_t> class_count;
  for (size_t i = 0; i < sampleIDs[nodeID].size(); ++i) {
    double value = data->get(sampleIDs[nodeID][i], dependent_varID);
    ++class_count[value];
  }
  return (mostFrequentValue(class_count, random_number_generator));
}

void TreeClassification::appendToFileInternal(std::ofstream& file) {
  // Empty on purpose
}

bool TreeClassification::splitNodeInternal(size_t nodeID, std::unordered_set<size_t>& possible_split_varIDs) {

  // TODO: <= okay? should it not be < ?
  // Check node size, stop if maximum reached
  if (sampleIDs[nodeID].size() <= min_node_size) {
    split_values[nodeID] = estimate(nodeID);
    return true;
  }

  // Check if node is pure and set split_value to estimate and stop if pure
  bool pure = true;
  double pure_value = 0;
  for (size_t i = 0; i < sampleIDs[nodeID].size(); ++i) {
    double value = data->get(sampleIDs[nodeID][i], dependent_varID);
    if (i != 0 && value != pure_value) {
      pure = false;
      break;
    }
    pure_value = value;
  }
  if (pure) {
    split_values[nodeID] = pure_value;
    return true;
  }

  // Find best split, stop if no decrease of impurity
  bool stop = findBestSplit(nodeID, possible_split_varIDs);
  if (stop) {
    split_values[nodeID] = estimate(nodeID);
    return true;
  }

  return false;
}

void TreeClassification::createEmptyNodeInternal() {
  // Empty on purpose
}

double TreeClassification::computePredictionAccuracyInternal() {

  size_t num_predictions = predictions[0].size();
  size_t num_missclassifications = 0;
  for (size_t i = 0; i < num_predictions; ++i) {
    double predicted_value = predictions[0][i];
    double real_value = data->get(oob_sampleIDs[i], dependent_varID);
    if (predicted_value != real_value) {
      ++num_missclassifications;
    }
  }
  return (1.0 - (double) num_missclassifications / (double) num_predictions);
}

bool TreeClassification::findBestSplit(size_t nodeID, std::unordered_set<size_t>& possible_split_varIDs) {

  size_t num_classes = class_values->size();
  double best_decrease = -1;
  size_t best_varID = 0;
  double best_value = 0;

  size_t* class_counts_left = new size_t[num_classes];
  size_t* class_counts_right = new size_t[num_classes];

  // For all possible split variables
  for (auto& varID : possible_split_varIDs) {

    // Create possible split values
    std::vector<double> possible_split_values;
    data->getAllValues(possible_split_values, sampleIDs[nodeID], varID);

    //Try next variable if all equal for this
    if (possible_split_values.size() < 2) {
      continue;
    }

    // For all possible split values
    for (auto& split_value : possible_split_values) {

      // Virtually split at this value. Count overall and for classes.
      size_t n_left = 0;
      size_t n_right = 0;
      for (size_t i = 0; i < num_classes; ++i) {
        class_counts_left[i] = 0;
        class_counts_right[i] = 0;
      }

      for (auto& sampleID : sampleIDs[nodeID]) {
        double value = data->get(sampleID, varID);
        uint sample_classID = (*response_classIDs)[sampleID];
        if (value <= split_value) {
          ++n_left;
          ++class_counts_left[sample_classID];
        } else {
          ++n_right;
          ++class_counts_right[sample_classID];
        }
      }

      // Stop if one child empty
      if (n_left == 0 || n_right == 0) {
        continue;
      }

      // Sum of squares
      double sum_left = 0;
      double sum_right = 0;
      for (size_t i = 0; i < num_classes; ++i) {
        sum_left += class_counts_left[i] * class_counts_left[i];
        sum_right += class_counts_right[i] * class_counts_right[i];
      }

      // Decrease of impurity
      double decrease = sum_left / (double) n_left + sum_right / (double) n_right;

      // If better than before, use this
      if (decrease > best_decrease) {
        best_value = split_value;
        best_varID = varID;
        best_decrease = decrease;
      }
    }
  }

  delete[] class_counts_left;
  delete[] class_counts_right;

  // Stop if no good split found
  if (best_decrease < 0) {
    return true;
  }

  // Save best values
  split_varIDs[nodeID] = best_varID;
  split_values[nodeID] = best_value;

  // Compute gini index for this node and to variable importance if needed
  if (importance_mode == IMP_GINI) {
    addGiniImportance(nodeID, best_varID, best_decrease);
  }
  return false;
}

void TreeClassification::addGiniImportance(size_t nodeID, size_t varID, double decrease) {

  std::vector<size_t> class_counts;
  class_counts.resize(class_values->size(), 0);

  for (auto& sampleID : sampleIDs[nodeID]) {
    uint sample_classID = (*response_classIDs)[sampleID];
    class_counts[sample_classID]++;
  }
  double sum_node = 0;
  for (auto& class_count : class_counts) {
    sum_node += class_count * class_count;
  }
  double best_gini = decrease - sum_node / (double) sampleIDs[nodeID].size();

  // No variable importance for no split variables
  size_t tempvarID = varID;
  for (auto& skip : *no_split_variables) {
    if (varID >= skip) {
      --tempvarID;
    }
  }
  variable_importance[tempvarID] += best_gini;
}

