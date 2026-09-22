#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <random>

class RandomNameGenerator {
private:
    std::vector<std::string> animals = {
        "Bear", "Eagle", "Falcon", "Fox", "Lion", "Otter", "Panther", "Tiger", "Wolf"
    };

    std::vector<std::string> objects = {
        "Anchor", "Compass", "Crystal", "Hammer", "Lantern", "Mirror", "Shield", "Sword"
    };

    std::vector<std::string> verbs = {
        "Dancing", "Flying", "Howling", "Leaping", "Running", "Soaring", "Stalking", "Swimming"
    };

    std::vector<std::string> nouns = {
        "Breeze", "Canyon", "Forest", "Mountain", "Ocean", "River", "Shadow", "Thunder"
    };

    std::mt19937 rng;

public:
    RandomNameGenerator() : rng(std::random_device{}()) {}

    static std::string
    Generate() {
        RandomNameGenerator g;
        std::uniform_int_distribution<size_t> animDist(0, g.animals.size() - 1);
        std::uniform_int_distribution<size_t> objDist(0,  g.objects.size() - 1);
        std::uniform_int_distribution<size_t> verbDist(0, g.verbs.size() - 1);
        std::uniform_int_distribution<size_t> nounDist(0, g.nouns.size() - 1);
        std::uniform_int_distribution<int> numDist(100, 999);

        std::string name = g.animals[animDist(g.rng)] +
                           g.objects[objDist(g.rng)] +
                           g.verbs[verbDist(g.rng)] +
                           g.nouns[nounDist(g.rng)] +
                           "#" +
                           std::to_string(numDist(g.rng));

        return name;
    }

};

