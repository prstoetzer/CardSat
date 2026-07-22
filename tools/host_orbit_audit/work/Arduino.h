#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
class String : public std::string {
public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(double v) : std::string(std::to_string(v)) {}
  String(int v) : std::string(std::to_string(v)) {}
};
class Stream {};
class File;    // referenced by satdb.h signatures the harness never calls
