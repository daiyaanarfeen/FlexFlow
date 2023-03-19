/* Copyright 2022 NVIDIA CORPORATION
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __LEGION_TRITON_STRATEGY_H__
#define __LEGION_TRITON_STRATEGY_H__

#include "config.h"
#include "legion.h"
#include "legion/legion_mapping.h"
#include "mappers/null_mapper.h"
#include "types.h"

namespace triton {
namespace backend {
namespace legion {

struct LayerStrategy;

class ShardingFunction : public Legion::ShardingFunctor {
public:
  ShardingFunction(Legion::ShardingID sid, LayerStrategy const *strategy);

public:
  Realm::Processor find_proc(Legion::DomainPoint const &point,
                             Legion::Domain const &domain);
  Legion::ShardID shard(Legion::DomainPoint const &point,
                        Legion::Domain const &full_space,
                        const size_t total_size);

public:
  const Legion::ShardingID sharding_id;
  LayerStrategy const *const strategy;
};

struct LayerStrategy {
public:
  LayerStrategy(Legion::ShardingID sid,
                Legion::MappingTagID tag,
                Legion::Runtime *runtime);
  ~LayerStrategy(void);

public:
  Legion::Domain get_launch_domain(void) const;
  // 'global' domain should be inclusive
  Legion::Domain find_local_domain(Realm::Processor proc,
                                   Legion::Domain const &global) const;
  bool is_local_processor(Realm::Processor proc) const;
  unsigned find_local_offset(Realm::Processor proc) const;
  Legion::DomainPoint find_local_point(Realm::Processor proc) const;

public:
  ShardingFunction *const sharding_function;
  const Legion::MappingTagID tag;

public:
  Realm::Processor::Kind kind;
  int nDims, dim[LEGION_MAX_DIM];
  unsigned nProcs;
  Realm::Processor local_processors[MAX_LOCAL_PROCS];
  Legion::DomainPoint local_points[MAX_LOCAL_PROCS];
  std::vector<Realm::Processor> global_processors;
};

struct PartitionStrategy {
public:
  PartitionStrategy(LegionModelState *model,
                    std::vector<LayerStrategy const *> &&layers)
      : layers(std::move(layers)) {}
  ~PartitionStrategy(void);

public:
  const std::vector<LayerStrategy const *> layers;

public:
  static PartitionStrategy *LoadStrategy(std::string const &filename,
                                         LegionModelState *model);
};

class StrategyMapper : public Legion::Mapping::Mapper {
public:
  struct FieldMemInfo {
  public:
    FieldMemInfo(void) {}
    FieldMemInfo(Legion::RegionTreeID t, Legion::FieldID f, Legion::Memory m)
        : tid(t), fid(f), memory(m) {}

  public:
    inline bool operator==(FieldMemInfo const &rhs) const {
      if (tid != rhs.tid) {
        return false;
      }
      if (fid != rhs.fid) {
        return false;
      }
      if (memory != rhs.memory) {
        return false;
      }
      return true;
    }
    inline bool operator<(FieldMemInfo const &rhs) const {
      if (tid < rhs.tid) {
        return true;
      }
      if (tid > rhs.tid) {
        return false;
      }
      if (fid < rhs.fid) {
        return true;
      }
      if (fid > rhs.fid) {
        return false;
      }
      return memory < rhs.memory;
    }

  public:
    Legion::RegionTreeID tid;
    Legion::FieldID fid;
    Legion::Memory memory;
  };
  struct InstanceInfo {
  public:
    InstanceInfo(void) {}
    InstanceInfo(Legion::LogicalRegion r,
                 Legion::Domain const &b,
                 Legion::Mapping::PhysicalInstance inst)
        : instance(inst), bounding_box(b) {
      regions.push_back(r);
    }

  public:
    Legion::Mapping::PhysicalInstance instance;
    Legion::Domain bounding_box;
    std::vector<Legion::LogicalRegion> regions;
  };
  struct InstanceInfos {
  public:
    inline bool has_instance(Legion::LogicalRegion region,
                             Legion::Mapping::PhysicalInstance &result) const {
      std::map<Legion::LogicalRegion, unsigned>::const_iterator finder =
          region_mapping.find(region);
      if (finder == region_mapping.end()) {
        return false;
      }
      InstanceInfo const &info = instances[finder->second];
      result = info.instance;
      return true;
    }

  public:
    inline unsigned insert(Legion::LogicalRegion region,
                           Legion::Domain const &bound,
                           Legion::Mapping::PhysicalInstance inst) {
      unsigned index = instances.size();
      for (unsigned idx = 0; idx < instances.size(); idx++) {
        if (inst != instances[idx].instance) {
          continue;
        }
        index = idx;
        break;
      }
      if (index == instances.size()) {
        instances.push_back(InstanceInfo(region, bound, inst));
      }
      region_mapping[region] = index;
      return index;
    }
    inline bool filter(Legion::Mapping::PhysicalInstance const &inst) {
      for (unsigned idx = 0; idx < instances.size(); idx++) {
        if (instances[idx].instance != inst) {
          continue;
        }
        // We also need to update any of the other region mappings
        for (std::map<Legion::LogicalRegion, unsigned>::iterator it =
                 region_mapping.begin();
             it != region_mapping.end();
             /*nothing*/) {
          if (it->second == idx) {
            std::map<Legion::LogicalRegion, unsigned>::iterator to_delete =
                it++;
            region_mapping.erase(to_delete);
          } else {
            if (it->second > idx) {
              it->second--;
            }
            it++;
          }
        }
        instances.erase(instances.begin() + idx);
        break;
      }
      return instances.empty();
    }

  public:
    // A list of instances that we have for this field in this memory
    std::vector<InstanceInfo> instances;
    // Mapping for logical regions that we already know have instances
    std::map<Legion::LogicalRegion, unsigned> region_mapping;
  };

public:
  StrategyMapper(PartitionStrategy const *strategy,
                 Legion::Mapping::MapperRuntime *runtime,
                 Legion::Machine machine);
  virtual ~StrategyMapper(void);

protected:
  // Start-up methods
  static Legion::AddressSpaceID get_local_node(void);
  static size_t get_total_nodes(Legion::Machine m);
  static char const *create_name(Legion::AddressSpace node);

public:
  virtual char const *get_mapper_name(void) const;
  virtual Legion::Mapping::Mapper::MapperSyncModel
      get_mapper_sync_model(void) const;
  virtual bool request_valid_instances(void) const {
    return false;
  }

public: // Task mapping calls
  virtual void select_task_options(const Legion::Mapping::MapperContext ctx,
                                   Legion::Task const &task,
                                   TaskOptions &output);
  virtual void premap_task(const Legion::Mapping::MapperContext ctx,
                           Legion::Task const &task,
                           PremapTaskInput const &input,
                           PremapTaskOutput &output);
  virtual void slice_task(const Legion::Mapping::MapperContext ctx,
                          Legion::Task const &task,
                          SliceTaskInput const &input,
                          SliceTaskOutput &output);
  virtual void map_task(const Legion::Mapping::MapperContext ctx,
                        Legion::Task const &task,
                        MapTaskInput const &input,
                        MapTaskOutput &output);
  virtual void map_replicate_task(const Legion::Mapping::MapperContext ctx,
                                  Legion::Task const &task,
                                  MapTaskInput const &input,
                                  MapTaskOutput const &default_output,
                                  MapReplicateTaskOutput &output);
  virtual void select_task_variant(const Legion::Mapping::MapperContext ctx,
                                   Legion::Task const &task,
                                   SelectVariantInput const &input,
                                   SelectVariantOutput &output);
  virtual void postmap_task(const Legion::Mapping::MapperContext ctx,
                            Legion::Task const &task,
                            PostMapInput const &input,
                            PostMapOutput &output);
  virtual void select_task_sources(const Legion::Mapping::MapperContext ctx,
                                   Legion::Task const &task,
                                   SelectTaskSrcInput const &input,
                                   SelectTaskSrcOutput &output);
  virtual void speculate(const Legion::Mapping::MapperContext ctx,
                         Legion::Task const &task,
                         SpeculativeOutput &output);
  virtual void report_profiling(const Legion::Mapping::MapperContext ctx,
                                Legion::Task const &task,
                                TaskProfilingInfo const &input);
  virtual void select_sharding_functor(const Legion::Mapping::MapperContext ctx,
                                       Legion::Task const &task,
                                       SelectShardingFunctorInput const &input,
                                       SelectShardingFunctorOutput &output);
  ShardingFunction *find_sharding_functor(Legion::Mappable const &mappable);

public: // Inline mapping calls
  virtual void map_inline(const Legion::Mapping::MapperContext ctx,
                          Legion::InlineMapping const &inline_op,
                          MapInlineInput const &input,
                          MapInlineOutput &output);
  virtual void select_inline_sources(const Legion::Mapping::MapperContext ctx,
                                     Legion::InlineMapping const &inline_op,
                                     SelectInlineSrcInput const &input,
                                     SelectInlineSrcOutput &output);
  virtual void report_profiling(const Legion::Mapping::MapperContext ctx,
                                Legion::InlineMapping const &inline_op,
                                InlineProfilingInfo const &input);

public: // Copy mapping calls
  virtual void map_copy(const Legion::Mapping::MapperContext ctx,
                        Legion::Copy const &copy,
                        MapCopyInput const &input,
                        MapCopyOutput &output);
  virtual void select_copy_sources(const Legion::Mapping::MapperContext ctx,
                                   Legion::Copy const &copy,
                                   SelectCopySrcInput const &input,
                                   SelectCopySrcOutput &output);
  virtual void speculate(const Legion::Mapping::MapperContext ctx,
                         Legion::Copy const &copy,
                         SpeculativeOutput &output);
  virtual void report_profiling(const Legion::Mapping::MapperContext ctx,
                                Legion::Copy const &copy,
                                CopyProfilingInfo const &input);
  virtual void select_sharding_functor(const Legion::Mapping::MapperContext ctx,
                                       Legion::Copy const &copy,
                                       SelectShardingFunctorInput const &input,
                                       SelectShardingFunctorOutput &output);

public: // Close mapping calls
  virtual void map_close(const Legion::Mapping::MapperContext ctx,
                         Legion::Close const &close,
                         MapCloseInput const &input,
                         MapCloseOutput &output);
  virtual void select_close_sources(const Legion::Mapping::MapperContext ctx,
                                    Legion::Close const &close,
                                    SelectCloseSrcInput const &input,
                                    SelectCloseSrcOutput &output);
  virtual void report_profiling(const Legion::Mapping::MapperContext ctx,
                                Legion::Close const &close,
                                CloseProfilingInfo const &input);
  virtual void select_sharding_functor(const Legion::Mapping::MapperContext ctx,
                                       Legion::Close const &close,
                                       SelectShardingFunctorInput const &input,
                                       SelectShardingFunctorOutput &output);

public: // Acquire mapping calls
  virtual void map_acquire(const Legion::Mapping::MapperContext ctx,
                           Legion::Acquire const &acquire,
                           MapAcquireInput const &input,
                           MapAcquireOutput &output);
  virtual void speculate(const Legion::Mapping::MapperContext ctx,
                         Legion::Acquire const &acquire,
                         SpeculativeOutput &output);
  virtual void report_profiling(const Legion::Mapping::MapperContext ctx,
                                Legion::Acquire const &acquire,
                                AcquireProfilingInfo const &input);
  virtual void select_sharding_functor(const Legion::Mapping::MapperContext ctx,
                                       Legion::Acquire const &acquire,
                                       SelectShardingFunctorInput const &input,
                                       SelectShardingFunctorOutput &output);

public: // Release mapping calls
  virtual void map_release(const Legion::Mapping::MapperContext ctx,
                           Legion::Release const &release,
                           MapReleaseInput const &input,
                           MapReleaseOutput &output);
  virtual void select_release_sources(const Legion::Mapping::MapperContext ctx,
                                      Legion::Release const &release,
                                      SelectReleaseSrcInput const &input,
                                      SelectReleaseSrcOutput &output);
  virtual void speculate(const Legion::Mapping::MapperContext ctx,
                         Legion::Release const &release,
                         SpeculativeOutput &output);
  virtual void report_profiling(const Legion::Mapping::MapperContext ctx,
                                Legion::Release const &release,
                                ReleaseProfilingInfo const &input);
  virtual void select_sharding_functor(const Legion::Mapping::MapperContext ctx,
                                       Legion::Release const &release,
                                       SelectShardingFunctorInput const &input,
                                       SelectShardingFunctorOutput &output);

public: // Partition mapping calls
  virtual void
      select_partition_projection(const Legion::Mapping::MapperContext ctx,
                                  Legion::Partition const &partition,
                                  SelectPartitionProjectionInput const &input,
                                  SelectPartitionProjectionOutput &output);
  virtual void map_partition(const Legion::Mapping::MapperContext ctx,
                             Legion::Partition const &partition,
                             MapPartitionInput const &input,
                             MapPartitionOutput &output);
  virtual void
      select_partition_sources(const Legion::Mapping::MapperContext ctx,
                               Legion::Partition const &partition,
                               SelectPartitionSrcInput const &input,
                               SelectPartitionSrcOutput &output);
  virtual void report_profiling(const Legion::Mapping::MapperContext ctx,
                                Legion::Partition const &partition,
                                PartitionProfilingInfo const &input);
  virtual void select_sharding_functor(const Legion::Mapping::MapperContext ctx,
                                       Legion::Partition const &partition,
                                       SelectShardingFunctorInput const &input,
                                       SelectShardingFunctorOutput &output);

public: // Fill mapper calls
  virtual void select_sharding_functor(const Legion::Mapping::MapperContext ctx,
                                       Legion::Fill const &fill,
                                       SelectShardingFunctorInput const &input,
                                       SelectShardingFunctorOutput &output);

public: // Task execution mapping calls
  virtual void configure_context(const Legion::Mapping::MapperContext ctx,
                                 Legion::Task const &task,
                                 ContextConfigOutput &output);
  virtual void select_tunable_value(const Legion::Mapping::MapperContext ctx,
                                    Legion::Task const &task,
                                    SelectTunableInput const &input,
                                    SelectTunableOutput &output);

public: // Must epoch mapping
  virtual void select_sharding_functor(const Legion::Mapping::MapperContext ctx,
                                       Legion::MustEpoch const &epoch,
                                       SelectShardingFunctorInput const &input,
                                       MustEpochShardingFunctorOutput &output);
  virtual void memoize_operation(const Legion::Mapping::MapperContext ctx,
                                 Legion::Mappable const &mappable,
                                 MemoizeInput const &input,
                                 MemoizeOutput &output);
  virtual void map_must_epoch(const Legion::Mapping::MapperContext ctx,
                              MapMustEpochInput const &input,
                              MapMustEpochOutput &output);

public: // Dataflow graph mapping
  virtual void map_dataflow_graph(const Legion::Mapping::MapperContext ctx,
                                  MapDataflowGraphInput const &input,
                                  MapDataflowGraphOutput &output);

public: // Mapping control and stealing
  virtual void select_tasks_to_map(const Legion::Mapping::MapperContext ctx,
                                   SelectMappingInput const &input,
                                   SelectMappingOutput &output);
  virtual void select_steal_targets(const Legion::Mapping::MapperContext ctx,
                                    SelectStealingInput const &input,
                                    SelectStealingOutput &output);
  virtual void permit_steal_request(const Legion::Mapping::MapperContext ctx,
                                    StealRequestInput const &intput,
                                    StealRequestOutput &output);

public: // handling
  virtual void handle_message(const Legion::Mapping::MapperContext ctx,
                              MapperMessage const &message);
  virtual void handle_task_result(const Legion::Mapping::MapperContext ctx,
                                  MapperTaskResult const &result);

protected:
  bool find_existing_instance(Legion::LogicalRegion region,
                              Legion::FieldID fid,
                              Legion::Memory target_memory,
                              Legion::Mapping::PhysicalInstance &result);
  bool map_tensor(const Legion::Mapping::MapperContext ctx,
                  Legion::Mappable const &mappable,
                  unsigned index,
                  Legion::LogicalRegion region,
                  Legion::FieldID fid,
                  Legion::Memory target_memory,
                  Legion::Processor target_proc,
                  std::vector<Legion::Mapping::PhysicalInstance> const &valid,
                  Legion::Mapping::PhysicalInstance &result,
                  Legion::ReductionOpID redop = 0);
  void filter_failed_acquires(
      std::vector<Legion::Mapping::PhysicalInstance> &needed_acquires,
      std::set<Legion::Mapping::PhysicalInstance> &failed_acquires);
  void report_failed_mapping(Legion::Mappable const &mappable,
                             unsigned index,
                             Legion::Memory target_memory,
                             Legion::ReductionOpID redop);
  void triton_select_sources(
      const Legion::Mapping::MapperContext ctx,
      Legion::Mapping::PhysicalInstance const &target,
      std::vector<Legion::Mapping::PhysicalInstance> const &sources,
      std::deque<Legion::Mapping::PhysicalInstance> &ranking);
  bool has_variant(const Legion::Mapping::MapperContext ctx,
                   Legion::Task const &task,
                   Legion::Processor::Kind kind);
  Legion::VariantID find_variant(const Legion::Mapping::MapperContext ctx,
                                 Legion::Task const &task);
  Legion::VariantID find_variant(const Legion::Mapping::MapperContext ctx,
                                 Legion::Task const &task,
                                 Legion::Processor target_proc);
  void pack_tunable(int const value, Mapper::SelectTunableOutput &output);

protected:
  static inline bool physical_sort_func(
      std::pair<Legion::Mapping::PhysicalInstance, unsigned> const &left,
      std::pair<Legion::Mapping::PhysicalInstance, unsigned> const &right) {
    return (left.second < right.second);
  }

public:
  PartitionStrategy const *const strategy;
  const Legion::Machine machine;
  const Legion::AddressSpace local_node;
  const size_t total_nodes;
  char const *const mapper_name;

protected:
  std::vector<Legion::Processor> local_cpus;
  std::vector<Legion::Processor> local_gpus;
  std::vector<Legion::Processor> local_omps; // OpenMP processors
  std::vector<Legion::Processor> local_ios;  // I/O processors
  std::vector<Legion::Processor> local_pys;  // Python processors
protected:
  Legion::Memory local_system_memory, local_zerocopy_memory;
  std::map<Legion::Processor, Legion::Memory> local_frame_buffers;
  std::map<Legion::Processor, Legion::Memory> local_numa_domains;

protected:
  std::map<std::pair<Legion::TaskID, Legion::Processor::Kind>,
           Legion::VariantID>
      used_variants;

protected:
  std::map<FieldMemInfo, InstanceInfos> local_instances;

protected:
  // These are used for computing sharding functions
  std::map<Legion::IndexPartition, unsigned> partition_color_space_dims;
  std::map<Legion::IndexSpace, unsigned> index_color_dims;
};

} // namespace legion
} // namespace backend
} // namespace triton

#endif // __LEGION_TRITON_STRATEGY_H__
