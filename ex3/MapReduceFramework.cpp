//
// Created by OWNER on 07-Jun-21.
//
#include "MapReduceFramework.h"
#include <atomic>
#include <pthread.h>
#include <cstdio>
#include <algorithm>
#include <map>
#include "Barrier.h"
struct comparison_func {;

    bool operator()(const K2 *a, const K2 *b) const {
        return *a < *b;
    }
};


class JobContext {
public:
    JobContext(const MapReduceClient &client,
               const InputVec &inputVec, OutputVec &outputVec,
               int multiThreadLevel) : client(client), inputVec(inputVec), outputVec(outputVec),
                                       multiThreadLevel(multiThreadLevel), threads(new pthread_t[multiThreadLevel]),
                                       atomic_counter(0), simple_counter(0), process_stage(0),
                                       mutex_for_inside(PTHREAD_MUTEX_INITIALIZER),
                                       mutex_for_outside(PTHREAD_MUTEX_INITIALIZER),
                                       locker(new Barrier(multiThreadLevel)) {
        for_shuffle = true;
        is_finish = false;
        process_stage = (inputVec.size() << 33) + (unsigned long) MAP_STAGE;

    }
    bool for_shuffle;
    const MapReduceClient &client;
    const InputVec &inputVec;
    OutputVec &outputVec;
    int multiThreadLevel;
    pthread_t *threads;
    std::atomic<int> atomic_counter;
    int simple_counter;
    std::atomic<uint64_t> process_stage;
    std::vector<IntermediateVec> intermediate_of_all_threads;
    pthread_mutex_t mutex_for_inside;
    pthread_mutex_t mutex_for_outside;
    Barrier *locker;
    std::map<K2 *, IntermediateVec, comparison_func> ordered_k2;
    bool is_finish;


    void lock(pthread_mutex_t &mutex) {
        if (pthread_mutex_lock(&mutex) != 0) {
            fprintf(stderr, "system error: mutex error on pthread_mutex_lock\n");
            exit(1);
        }
    }

    void unlock(pthread_mutex_t &mutex) {
        if (pthread_mutex_unlock(&mutex) != 0) {
            fprintf(stderr, "system error: mutex error on pthread_mutex_unlock\n");
            exit(1);
        }
    }

    ~JobContext() {
        if (int i = pthread_mutex_destroy(&mutex_for_inside)) {
            fprintf(stderr, "system error: system error on pthread_mutex_destroy %d \n", i);
            exit(1);
        }
        if (pthread_mutex_destroy(&mutex_for_outside)) {
            fprintf(stderr, "system error: system error on pthread_mutex_destroy \n");
            exit(1);
        }
        delete[] threads;
        delete locker;
    }


};

void shuffle(JobContext *&ourjob) {
    if (ourjob->for_shuffle) {
        ourjob->for_shuffle = false;
        ourjob->process_stage = ((unsigned long) ourjob->simple_counter << 33) + (unsigned long) SHUFFLE_STAGE;
        for (auto &intermediate_vec : ourjob->intermediate_of_all_threads) {
            while (!(intermediate_vec.empty())) {
                if (ourjob->ordered_k2.count(intermediate_vec.back().first)) {
                    ourjob->ordered_k2[intermediate_vec.back().first].push_back(intermediate_vec.back());
                    intermediate_vec.pop_back();
                } else {
                    IntermediateVec vec;
                    vec.push_back(intermediate_vec.back());
                    ourjob->ordered_k2[intermediate_vec.back().first] = vec;
                    intermediate_vec.pop_back();
                }
            }
            ourjob->process_stage += 1 << 2;
        }
        ourjob->process_stage = ((unsigned long) ourjob->simple_counter << 33) + (unsigned long) REDUCE_STAGE;
    }
}

void emit2(K2 *key, V2 *value, void *context) {
    std::pair<K2 *, V2 *> product1(key, value);
    auto *vec = (IntermediateVec *) context;
    vec->push_back(product1);
}

void emit3(K3 *key, V3 *value, void *context) {

    auto *ourjob = (JobContext *) context;
    ourjob->lock(ourjob->mutex_for_inside);
    std::pair<K3 *, V3 *> product1(key, value);
    ourjob->outputVec.push_back(product1);
    ourjob->unlock(ourjob->mutex_for_inside);

}

void *process_for_threads(void *arg) {

    auto *ourjob = (JobContext *) arg;
    IntermediateVec intermediate_of_thread;

    //// map stage
    for (int i = ourjob->atomic_counter++; i < ourjob->inputVec.size(); i = ourjob->atomic_counter++) {
        auto pair = ourjob->inputVec.at(static_cast<unsigned long>(i));
        ourjob->process_stage += 1 << 2;
        ourjob->client.map(pair.first, pair.second, &intermediate_of_thread);
    }
    ourjob->lock(ourjob->mutex_for_inside);
    ourjob->simple_counter += intermediate_of_thread.size();
    ourjob->intermediate_of_all_threads.push_back(intermediate_of_thread);
    ourjob->unlock(ourjob->mutex_for_inside);
    ourjob->locker->barrier();

    ////shuffle stage
    ourjob->lock(ourjob->mutex_for_inside);
    shuffle(ourjob);
    ourjob->unlock(ourjob->mutex_for_inside);


    ////reduce stage
    ourjob->lock(ourjob->mutex_for_inside);
    while (!(ourjob->ordered_k2.empty())) {
        K2 *key = ourjob->ordered_k2.begin()->first;
        IntermediateVec vec = ourjob->ordered_k2.at(key);
        unsigned long a = vec.size();
        ourjob->ordered_k2.erase(key);
        ourjob->unlock(ourjob->mutex_for_inside);
        ourjob->client.reduce(&vec, ourjob);
        ourjob->lock(ourjob->mutex_for_inside);
        ourjob->process_stage += a << 2;

    }
    ourjob->unlock(ourjob->mutex_for_inside);
    pthread_exit(nullptr);
}


JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel){
    auto *ourjob = new JobContext(client, inputVec, outputVec, multiThreadLevel);
    for (int i = 0; i < multiThreadLevel; ++i)
    {
        if (pthread_create(&(ourjob->threads[i]), nullptr, &process_for_threads, ourjob) != 0)
        {
            fprintf(stderr, "system error: system error on pthread_create \n");
            exit(1);
        }
    }
    return ourjob;
}

void waitForJob(JobHandle job) {
    auto *ourjob = (JobContext *) job;
    ourjob->lock(ourjob->mutex_for_outside);
    if (!ourjob->is_finish)
    {
        for (int i = 0; i < ourjob->multiThreadLevel; ++i)
        {
            if (pthread_join(ourjob->threads[i], nullptr) != 0)
            {
                fprintf(stderr, "system error: system error on pthread_join \n");
                exit(1);
            }

        }
        ourjob->is_finish = true;
    }
    ourjob->unlock(ourjob->mutex_for_outside);
}

void getJobState(JobHandle job, JobState *state) {
    auto *ourjob = (JobContext *) job;
    state->stage = UNDEFINED_STAGE;
    state->percentage = 0;
    unsigned long current = ourjob->process_stage.load();
    unsigned long elements_at_stage_num = current >> 33;
    unsigned long counter = current >> 2 & (((uint64_t) 1 << 31) - 1);
    state->stage = static_cast<stage_t>(current & 3);
    if (elements_at_stage_num) {
        state->percentage = 100 * (counter / (float) elements_at_stage_num);
    }
}

void closeJobHandle(JobHandle job) {
    auto *ourjob = (JobContext *) job;
    waitForJob(ourjob);
    delete ourjob;
}