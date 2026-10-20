#pragma once
// Minimal replacement for boost::adjacency_list + connected_components used by planegcs GCS.cpp
#include <numeric>
#include <vector>
namespace boost {
struct vecS {}; struct undirectedS {};
template<class, class, class> struct adjacency_list { std::vector<std::vector<int>> adj; };
template<class G> inline int add_vertex(G& g){ g.adj.emplace_back(); return int(g.adj.size())-1; }
template<class G> inline void add_edge(int a, int b, G& g){ g.adj[a].push_back(b); g.adj[b].push_back(a); }
template<class G> inline std::size_t num_vertices(const G& g){ return g.adj.size(); }
template<class G> inline int connected_components(const G& g, int* comp){
  const int n = int(g.adj.size()); std::vector<int> st; int c = 0;
  for (int i=0;i<n;++i) comp[i] = -1;
  for (int i=0;i<n;++i){ if (comp[i]>=0) continue; comp[i]=c; st.push_back(i);
    while(!st.empty()){ int v=st.back(); st.pop_back(); for(int w: g.adj[v]) if(comp[w]<0){comp[w]=c; st.push_back(w);} }
    ++c; }
  return c; }
}
