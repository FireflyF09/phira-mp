//! Procedural macros for Phira MP plugin system

use proc_macro::TokenStream;
use quote::quote;
use syn::{parse_macro_input, DeriveInput};

/// Derive macro for plugin metadata
#[proc_macro_derive(PluginMetadata)]
pub fn derive_plugin_metadata(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let name = &input.ident;
    
    let expanded = quote! {
        impl PluginMetadata for #name {
            fn name(&self) -> &str {
                &self.name
            }
            
            fn version(&self) -> &str {
                &self.version
            }
            
            fn author(&self) -> &str {
                &self.author
            }
            
            fn description(&self) -> Option<&str> {
                self.description.as_deref()
            }
            
            fn dependencies(&self) -> Option<&Vec<String>> {
                self.dependencies.as_ref()
            }
            
            fn permissions(&self) -> Option<&Vec<String>> {
                self.permissions.as_ref()
            }
        }
    };
    
    TokenStream::from(expanded)
}

/// Macro to declare a plugin
#[proc_macro_attribute]
pub fn plugin(_attr: TokenStream, item: TokenStream) -> TokenStream {
    // For now, just pass through
    // TODO: Generate plugin initialization code
    item
}