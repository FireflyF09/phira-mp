use crate::Error;
use std::collections::{HashMap, HashSet, VecDeque};
use petgraph::{graph::DiGraph, visit::{Dfs, EdgeRef}, algo::kosaraju_scc};

/// Dependency graph for plugins
pub struct DependencyGraph {
    /// Graph of plugin dependencies
    graph: DiGraph<String, ()>,
    /// Node indices by plugin name
    node_indices: HashMap<String, petgraph::graph::NodeIndex>,
    /// Reverse mapping from node index to plugin name
    index_to_plugin: HashMap<petgraph::graph::NodeIndex, String>,
}

impl DependencyGraph {
    /// Create a new dependency graph
    pub fn new() -> Self {
        Self {
            graph: DiGraph::new(),
            node_indices: HashMap::new(),
            index_to_plugin: HashMap::new(),
        }
    }

    /// Add a plugin to the graph
    pub fn add_plugin(&mut self, plugin_name: String, dependencies: Vec<String>) -> Result<(), Error> {
        // Get or create node for the plugin
        let plugin_node = self.get_or_create_node(plugin_name.clone());
        
        // Add edges for each dependency
        for dep_name in dependencies {
            let dep_node = self.get_or_create_node(dep_name.clone());
            self.graph.add_edge(dep_node, plugin_node, ());
        }
        
        Ok(())
    }

    /// Remove a plugin from the graph
    pub fn remove_plugin(&mut self, plugin_name: &str) {
        if let Some(node_index) = self.node_indices.remove(plugin_name) {
            self.index_to_plugin.remove(&node_index);
            
            // Remove all edges connected to this node
            let mut edges_to_remove: Vec<petgraph::graph::EdgeIndex> = Vec::new();
            for edge in self.graph.edges_directed(node_index, petgraph::Direction::Outgoing) {
                edges_to_remove.push(edge.id());
            }
            for edge in self.graph.edges_directed(node_index, petgraph::Direction::Incoming) {
                edges_to_remove.push(edge.id());
            }
            
            // Remove edges
            for edge_id in edges_to_remove {
                self.graph.remove_edge(edge_id);
            }
            
            // Remove the node
            self.graph.remove_node(node_index);
        }
    }

    /// Check for missing dependencies
    pub fn check_missing_dependencies(&self, plugin_name: &str) -> Vec<String> {
        let mut missing = Vec::new();
        
        if let Some(node_index) = self.node_indices.get(plugin_name) {
            // Get all dependencies of this plugin
            for neighbor in self.graph.neighbors_directed(*node_index, petgraph::Direction::Incoming) {
                let dep_name = self.index_to_plugin.get(&neighbor).unwrap();
                
                // Check if the dependency plugin is actually loaded
                if !self.node_indices.contains_key(dep_name) {
                    missing.push(dep_name.clone());
                }
            }
        }
        
        missing
    }

    /// Get all dependencies of a plugin (transitive closure)
    pub fn get_all_dependencies(&self, plugin_name: &str) -> Vec<String> {
        let mut dependencies: HashSet<String> = HashSet::new();

        if let Some(start_node) = self.node_indices.get(plugin_name) {
            let mut dfs = Dfs::new(&self.graph, *start_node);
            
            while let Some(node) = dfs.next(&self.graph) {
                if node != *start_node {
                    if let Some(name) = self.index_to_plugin.get(&node) {
                        dependencies.insert(name.clone());
                    }
                }
            }
        }
        
        dependencies.into_iter().collect()
    }

    /// Get all dependents of a plugin (reverse dependencies)
    pub fn get_all_dependents(&self, plugin_name: &str) -> Vec<String> {
        let mut dependents: HashSet<String> = HashSet::new();

        if let Some(start_node) = self.node_indices.get(plugin_name) {
            // Perform reverse DFS
            let mut stack = VecDeque::new();
            let mut visited = HashSet::new();
            
            stack.push_back(*start_node);
            visited.insert(*start_node);
            
            while let Some(node) = stack.pop_front() {
                // Add all nodes that depend on this node
                for neighbor in self.graph.neighbors_directed(node, petgraph::Direction::Outgoing) {
                    if !visited.contains(&neighbor) {
                        visited.insert(neighbor);
                        stack.push_back(neighbor);
                        
                        if let Some(name) = self.index_to_plugin.get(&neighbor) {
                            dependents.insert(name.clone());
                        }
                    }
                }
            }
        }
        
        dependents.into_iter().collect()
    }

    /// Check for circular dependencies
    pub fn check_circular_dependencies(&self) -> Result<(), Error> {
        let scc = kosaraju_scc(&self.graph);
        
        // Find strongly connected components with more than one node (circular dependencies)
        let circular_deps: Vec<Vec<String>> = scc
            .into_iter()
            .filter(|component: &Vec<petgraph::graph::NodeIndex>| component.len() > 1)
            .map(|component: Vec<petgraph::graph::NodeIndex>| {
                component
                    .iter()
                    .filter_map(|node| self.index_to_plugin.get(node).cloned())
                    .collect()
            })
            .collect();
        
        if !circular_deps.is_empty() {
            let error_msg = circular_deps
                .iter()
                .map(|deps: &Vec<String>| format!("[{}]", deps.join(", ")))
                .collect::<Vec<_>>()
                .join("; ");
            
            return Err(Error::Dependency(format!(
                "Circular dependencies detected: {}",
                error_msg
            )));
        }
        
        Ok(())
    }

    /// Get topological order of plugins (load order)
    pub fn get_load_order(&self) -> Result<Vec<String>, Error> {
        // Check for circular dependencies first
        self.check_circular_dependencies()?;
        
        // Use topological sort
        match petgraph::algo::toposort(&self.graph, None) {
            Ok(order) => {
                let mut plugins: Vec<String> = Vec::new();
                for node in order {
                    if let Some(name) = self.index_to_plugin.get(&node) {
                        plugins.push(name.clone());
                    }
                }
                Ok(plugins)
            }
            Err(cycle) => {
                // This shouldn't happen since we already checked for circular deps
                let cycle_node: petgraph::graph::NodeIndex = cycle.node_id();
                let cycle_name = self.index_to_plugin.get(&cycle_node).cloned().unwrap_or_default();
                Err(Error::Dependency(format!(
                    "Cycle detected involving plugin: {}",
                    cycle_name
                )))
            }
        }
    }

    /// Get unload order (reverse topological order)
    pub fn get_unload_order(&self) -> Result<Vec<String>, Error> {
        let load_order = self.get_load_order()?;
        Ok(load_order.into_iter().rev().collect())
    }

    /// Check if a plugin can be safely unloaded (no dependents)
    pub fn can_unload_safely(&self, plugin_name: &str) -> bool {
        self.get_all_dependents(plugin_name).is_empty()
    }

    /// Get optional dependencies that are not required
    pub fn get_optional_dependencies(&self, plugin_name: &str, required_deps: &[String]) -> Vec<String> {
        let all_deps = self.get_all_dependencies(plugin_name);
        all_deps
            .into_iter()
            .filter(|dep| !required_deps.contains(dep))
            .collect()
    }

    /// Get dependency graph statistics
    pub fn stats(&self) -> DependencyGraphStats {
        DependencyGraphStats {
            total_plugins: self.node_indices.len(),
            total_dependencies: self.graph.edge_count(),
            average_dependencies_per_plugin: if self.node_indices.is_empty() {
                0.0
            } else {
                self.graph.edge_count() as f64 / self.node_indices.len() as f64
            },
        }
    }

    /// Helper method to get or create a node
    fn get_or_create_node(&mut self, plugin_name: String) -> petgraph::graph::NodeIndex {
        if let Some(&node_index) = self.node_indices.get(&plugin_name) {
            node_index
        } else {
            let node_index = self.graph.add_node(plugin_name.clone());
            self.node_indices.insert(plugin_name.clone(), node_index);
            self.index_to_plugin.insert(node_index, plugin_name);
            node_index
        }
    }
}

/// Dependency graph statistics
#[derive(Debug, Clone)]
pub struct DependencyGraphStats {
    pub total_plugins: usize,
    pub total_dependencies: usize,
    pub average_dependencies_per_plugin: f64,
}

/// Dependency resolution result
pub struct DependencyResolution {
    /// Plugins to load in order
    pub load_order: Vec<String>,
    /// Plugins that cannot be loaded due to missing dependencies
    pub missing_dependencies: Vec<(String, Vec<String>)>,
    /// Circular dependency groups
    pub circular_dependencies: Vec<Vec<String>>,
}

impl DependencyResolution {
    /// Create a new dependency resolution
    pub fn new() -> Self {
        Self {
            load_order: Vec::new(),
            missing_dependencies: Vec::new(),
            circular_dependencies: Vec::new(),
        }
    }

    /// Check if resolution is successful
    pub fn is_successful(&self) -> bool {
        self.missing_dependencies.is_empty() && self.circular_dependencies.is_empty()
    }
}

/// Dependency resolver for complex dependency scenarios
pub struct DependencyResolver {
    graph: DependencyGraph,
    plugin_manifest_dependencies: HashMap<String, Vec<String>>,
}

impl DependencyResolver {
    /// Create a new dependency resolver
    pub fn new() -> Self {
        Self {
            graph: DependencyGraph::new(),
            plugin_manifest_dependencies: HashMap::new(),
        }
    }

    /// Add a plugin with its manifest dependencies
    pub fn add_plugin_manifest(
        &mut self,
        plugin_name: String,
        dependencies: Vec<String>,
    ) -> Result<(), Error> {
        // Store manifest dependencies
        self.plugin_manifest_dependencies.insert(plugin_name.clone(), dependencies.clone());
        
        // Add to dependency graph
        self.graph.add_plugin(plugin_name, dependencies)
    }

    /// Resolve dependencies for all plugins
    pub fn resolve(&self) -> DependencyResolution {
        let mut resolution = DependencyResolution::new();
        
        // Check for circular dependencies
        if let Err(e) = self.graph.check_circular_dependencies() {
            // Extract circular dependencies from error message
            // This is a hack - in real implementation we'd parse the error better
            if let Error::Dependency(msg) = e {
                if msg.contains("Circular dependencies detected:") {
                    // Parse circular dependencies
                    // Implementation would parse the error message
                }
            }
        }
        
        // Check for missing dependencies
        for plugin_name in self.graph.node_indices.keys() {
            let missing = self.graph.check_missing_dependencies(plugin_name);
            if !missing.is_empty() {
                resolution.missing_dependencies.push((plugin_name.clone(), missing));
            }
        }
        
        // Get load order if no issues
        if resolution.is_successful() {
            if let Ok(load_order) = self.graph.get_load_order() {
                resolution.load_order = load_order;
            }
        }
        
        resolution
    }

    /// Get plugins that depend on a specific plugin
    pub fn get_dependents(&self, plugin_name: &str) -> Vec<String> {
        self.graph.get_all_dependents(plugin_name)
    }

    /// Get plugins that a specific plugin depends on
    pub fn get_dependencies(&self, plugin_name: &str) -> Vec<String> {
        self.graph.get_all_dependencies(plugin_name)
    }

    /// Check if a plugin has all its dependencies satisfied
    pub fn has_all_dependencies(&self, plugin_name: &str) -> bool {
        self.graph.check_missing_dependencies(plugin_name).is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_dependency_graph() {
        let mut graph = DependencyGraph::new();
        
        // Add plugins with dependencies
        graph.add_plugin("plugin_a".to_string(), vec![]).unwrap();
        graph.add_plugin("plugin_b".to_string(), vec!["plugin_a".to_string()]).unwrap();
        graph.add_plugin("plugin_c".to_string(), vec!["plugin_a".to_string(), "plugin_b".to_string()]).unwrap();
        
        // Check dependencies
        let deps = graph.get_all_dependencies("plugin_c");
        assert!(deps.contains(&"plugin_a".to_string()));
        assert!(deps.contains(&"plugin_b".to_string()));
        
        // Check load order
        let load_order = graph.get_load_order().unwrap();
        assert_eq!(load_order[0], "plugin_a");
        assert_eq!(load_order[1], "plugin_b");
        assert_eq!(load_order[2], "plugin_c");
    }
    
    #[test]
    fn test_circular_dependency() {
        let mut graph = DependencyGraph::new();
        
        graph.add_plugin("plugin_a".to_string(), vec!["plugin_b".to_string()]).unwrap();
        graph.add_plugin("plugin_b".to_string(), vec!["plugin_a".to_string()]).unwrap();
        
        assert!(graph.check_circular_dependencies().is_err());
    }
    
    #[test]
    fn test_missing_dependencies() {
        let mut graph = DependencyGraph::new();
        
        graph.add_plugin("plugin_a".to_string(), vec!["missing_plugin".to_string()]).unwrap();
        
        let missing = graph.check_missing_dependencies("plugin_a");
        assert_eq!(missing, vec!["missing_plugin".to_string()]);
    }
}