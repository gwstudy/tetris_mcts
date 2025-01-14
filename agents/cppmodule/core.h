#ifndef CORE_H
#define CORE_H
#include<unordered_set>
#include<deque>
#include<array>
#include<algorithm>
#include<cstdlib>
#include<functional>
#include<cmath>
#include<special.h>
#include<iostream>
#include<pyTetris.h>
#include<pybind11/pybind11.h>
#include<pybind11/numpy.h>
#include<pybind11/stl.h>

const size_t n_actions = 7;

namespace py = pybind11;

/*
    Utilities
*/
template<typename T>
void print_container(T &c){
    for(auto v : c){
        std::cout << v << " ";
    }
    std::cout << std::endl;
}

std::unordered_set<size_t> get_all_childs(size_t index, py::array_t<int, 1> child){

    std::deque<size_t> to_traverse({index});

    std::unordered_set<size_t> traversed;

    for(size_t i=0; i < to_traverse.size(); ++i){
        auto pair = traversed.insert(to_traverse[i]);
        if(pair.second){
            for(int c=0; c < child.shape(1); ++c){
                size_t _c = *child.data(*pair.first, c);
                if(traversed.find(_c) == traversed.end())
                    to_traverse.push_back(_c);
            }
        }
    }

    return traversed;    
}

int _check_low(const std::vector<int> &indices, const std::vector<int> &count, int n){
    std::vector<int> low;
    for(int i : indices){
        if(count[i] < n)
            low.push_back(i);
    }

    if(low.empty())
        return 0;
    else
        return low[rand() % low.size()];
}

int check_low(const std::vector<int> &indices, const py::array_t<int, 1> &count, int n){
    auto count_uc = count.unchecked<1>();
    std::deque<int> low;
    for(int i : indices){
        if(count_uc(i) < n)
            low.push_back(i);
    }

    if(low.empty())
        return 0;
    else
        return low[rand() % low.size()];
}

/*
    POLICY
*/

int policy_clt(
        const std::vector<int> &nodes,
        const std::vector<int> &visit,
        const std::vector<float> &value,
        const std::vector<float> &variance){
    
    int n = std::accumulate(visit.begin(), visit.end(), 0);

    size_t max_idx = 0;
    float max_q = 0;
    float bound_coeff = norm_quantile(n);
    for(size_t i=0; i < value.size(); ++i){
        float q = value[i] + bound_coeff * sqrt(variance[i] / visit[i]);
        if(i == 0)
            max_q = q;
        else if(q > max_q){
            max_q = q;
            max_idx = i;
        }
    }

    return nodes[max_idx];
}

/*
    PROJECTION CORES
*/

void get_unique_child_obs(
        size_t index, 
        const py::array_t<int, 1> &child, 
        const py::array_t<float, 1> &score, 
        const py::array_t<int, 1> &n_to_o,
        std::vector<int> &c_nodes,
        std::vector<int> &c_obs){

    auto child_uc = child.unchecked<2>();
    auto score_uc = score.unchecked<1>();
    auto n_to_o_uc = n_to_o.unchecked<1>();

    c_nodes.clear();
    c_obs.clear();

    for(size_t i=0; i < n_actions; ++i){
        int c = child_uc(index, i);
        if(c == 0)
            continue;

        int o = n_to_o_uc(c);
        auto o_idx = std::find(c_obs.begin(), c_obs.end(), o);

        if(o_idx == c_obs.end()){
            c_nodes.push_back(c);
            c_obs.push_back(o);
        }else{
            size_t idx = std::distance(c_obs.begin(), o_idx);
            if(score_uc(c) > score_uc(c_nodes[idx]))
                c_nodes[idx] = c;
        }
    }

}

py::tuple get_unique_child_obs_(
        size_t index,
        const py::array_t<int, 1> &child, 
        const py::array_t<float, 1> &score, 
        const py::array_t<int, 1> &n_to_o){

    std::vector<int> c_nodes, c_obs;
    c_nodes.reserve(n_actions);
    c_obs.reserve(n_actions);

    get_unique_child_obs(index, child, score, n_to_o, c_nodes, c_obs);

    /*
    py::list py_c_nodes = py::cast(c_nodes);
    py::list py_c_obs = py::cast(c_obs);

    return py::make_tuple(py_c_nodes, py_c_obs);
    */
    return py::make_tuple(c_nodes, c_obs);
}

py::array_t<int, 1> select_trace_obs(
        size_t index, 
        py::array_t<int, 1> &child,
        py::array_t<int, 1> &visit,
        py::array_t<float, 1> &value,
        py::array_t<float, 1> &variance,
        py::array_t<float, 1> &score,
        py::array_t<int, 1> &n_to_o,
        size_t low){

    auto visit_uc = visit.unchecked<1>();
    auto value_uc = value.unchecked<1>();
    auto variance_uc = variance.unchecked<1>();
    auto score_uc = score.unchecked<1>();

    std::vector<int> trace;

    std::vector<int> _visit;
    std::vector<float> _value, _variance;

    _visit.reserve(n_actions);
    _value.reserve(n_actions);
    _variance.reserve(n_actions);

    std::vector<int> c_nodes, c_obs;
    c_nodes.reserve(n_actions);
    c_obs.reserve(n_actions);

    while(true){

        trace.push_back(index);

        get_unique_child_obs(index, child, score, n_to_o, c_nodes, c_obs);
        if(c_nodes.empty())
            break;
        int o = check_low(c_obs, visit, low);

        size_t _s = c_nodes.size();
        if(o == 0){
            _visit.resize(_s);
            _value.resize(_s);
            _variance.resize(_s);
            for(size_t i=0; i < _s; ++i){
                int _o = c_obs[i];
                int _c = c_nodes[i];
                _visit[i] = visit_uc(_o);
                _value[i] = value_uc(_o) + score_uc(_c) - score_uc(index);
                _variance[i] = variance_uc(_o);
            }
            index = policy_clt(c_nodes, _visit, _value, _variance);
        }else{
            auto o_it = std::find(c_obs.begin(), c_obs.end(), o);
            index = c_nodes[std::distance(c_obs.begin(), o_it)];
        }
        
    }
    return py::array_t<int, 1>(trace.size(), &trace[0]);
}

void backup_trace_obs(
        py::array_t<int, 1> &trace,
        py::array_t<int, 1> &visit,
        py::array_t<float, 1> &value,
        py::array_t<float, 1> &variance,
        py::array_t<int, 1> &n_to_o,
        py::array_t<float, 1> &score,
        double _value,
        double _variance,
        double gamma){

    auto trace_uc = trace.unchecked<1>();
    auto visit_uc = visit.mutable_unchecked<1>();
    auto value_uc = value.mutable_unchecked<1>();
    auto variance_uc = variance.mutable_unchecked<1>();
    auto score_uc = score.unchecked<1>();
    auto n_to_o_uc = n_to_o.unchecked<1>();

    for(int i=trace.size() - 1; i >= 0; --i){
        int idx = trace_uc(i);
        _value -= score_uc(idx);
        int o = n_to_o_uc(idx);
        if(visit_uc(o) == 0){
            value_uc(o) = _value;
            variance_uc(o) = _variance;
        }else{
            double delta = _value - value_uc(o);
            value_uc(o) += delta / (visit_uc(o) + 1);
            double delta2 = _value - value_uc(o);
            variance_uc(o) += (delta * delta2 - variance_uc(o)) / (visit_uc(o) + 1);
        }
        visit_uc(o) += 1;
        _value = gamma * _value + score_uc(idx); 
    }
}

void backup_trace_mixture_obs(
        py::array_t<int, 1> &trace,
        py::array_t<int, 1> &visit,
        py::array_t<float, 1> &value,
        py::array_t<float, 1> &variance,
        py::array_t<int, 1> &n_to_o,
        py::array_t<float, 1> &score,
        double _value,
        double _variance,
        double gamma){

    auto trace_uc = trace.unchecked<1>();
    auto visit_uc = visit.mutable_unchecked<1>();
    auto value_uc = value.mutable_unchecked<1>();
    auto variance_uc = variance.mutable_unchecked<1>();
    auto score_uc = score.unchecked<1>();
    auto n_to_o_uc = n_to_o.unchecked<1>();

    for(int i=trace.size() - 1; i >= 0; --i){
        int idx = trace_uc(i);
        _value -= score_uc(idx);
        int o = n_to_o_uc(idx);

        visit_uc(o) += 1;

        double v_sq_diff = _value * _value - value_uc(o) * value_uc(o);

        double v_tmp = value_uc(o);

        double delta = (_value - value_uc(o)) / visit_uc(o);
        value_uc(o) += delta;

        double var_diff = _variance - variance_uc(o);

        variance_uc(o) += (var_diff + v_sq_diff) / visit_uc(o) - delta * (v_tmp + value_uc(o));

        _value = gamma * _value + score_uc(idx);
        _variance *= (gamma * gamma);
    }
}

void backup_trace_obs_LP(
        py::array_t<int, 1> &trace,
        py::array_t<int, 1> &visit,
        py::array_t<float, 1> &value,
        py::array_t<float, 1> &variance,
        py::array_t<int, 1> &n_to_o,
        py::array_t<float, 1> &score,
        py::array_t<bool, 1> &end,
        std::vector<int> &_child,
        std::vector<int> &_obs,
        py::array_t<float, 1> &_value,
        py::array_t<float, 1> &_variance,
        double gamma,
        bool mixture,
        bool averaged){

    auto trace_uc = trace.unchecked<1>();
    auto visit_uc = visit.mutable_unchecked<1>();
    auto value_uc = value.mutable_unchecked<1>();
    auto variance_uc = variance.mutable_unchecked<1>();
    auto score_uc = score.unchecked<1>();
    auto end_uc = end.unchecked<1>();
    auto _value_uc = _value.unchecked<1>();
    auto _variance_uc = _variance.unchecked<1>();

    std::function<void(
        py::array_t<int, 1>&, py::array_t<int, 1>&, py::array_t<float, 1>&,
        py::array_t<float, 1>&, py::array_t<int, 1>&, py::array_t<float, 1>&, 
        double, double, double)> backup;

    if(mixture)
        backup = backup_trace_mixture_obs;
    else
        backup = backup_trace_obs;

    if(_child.size() > 0){
        double v_tmp, var_tmp;
        v_tmp = var_tmp = 0;
        for(size_t i=0; i < _child.size(); ++i){
            int __c = _child[i];
            int __o = _obs[i];
            if(visit_uc(__o) == 0){
                visit_uc(__o) += 1;
                if(end_uc(__c)){
                    value_uc(__o) = 0;
                    variance_uc(__o) = 0;
                }else{
                    value_uc(__o) = _value_uc(i);
                    variance_uc(__o) = _variance_uc(i);
                }
            }
            if(averaged){
                v_tmp += score_uc(__c) + gamma * value_uc(__o);
                var_tmp += variance_uc(__o);
            }else{
                backup(trace, visit, value, variance, n_to_o, score,
                       value_uc(__o) + gamma * score_uc(__c),
                       gamma * gamma * variance_uc(__o), gamma);
            }
        }
        if(averaged){
            v_tmp /= _child.size();
            var_tmp *= (gamma * gamma / _child.size());
            backup(trace, visit, value, variance, n_to_o, score, v_tmp, var_tmp, gamma);
        }
    }else{
        backup(trace, visit, value, variance, n_to_o, score,
               score_uc(trace_uc(trace.shape(0) - 1)), 0, gamma);
    }

    //if(mixture)
    //    backup_trace_mixture_obs(
    //        trace, visit, value, variance,
    //        n_to_o, score, val_mean, var_mean, gamma);
    //else
    //    backup_trace_obs(
    //        trace, visit, value, variance,
    //        n_to_o, score, val_mean, var_mean, gamma);
}

/*
   DISTRIBUTIONAL
*/

py::array_t<float> transform_distribution(py::array_t<float, 1> &dist, double vmin, double vmax, double shift, double scale){
    auto dist_uc = dist.unchecked<1>();
    int bins = dist.size();

    std::vector<float> result;
    result.resize(bins);

    double delta = (vmax - vmin) / bins;
    double bin_shift = shift / delta;
    for(int b=0; b < bins; ++b){
        double lb = std::max(b * scale + bin_shift, 0.);
        int b_lb = std::floor(lb);
        double ub = std::min(lb + scale, double(bins));
        int b_ub = std::floor(ub);

        double frac = b_ub - lb;

        result[b_lb] += dist_uc(b) * frac;
        result[b_ub] += dist_uc(b) * (1 - frac);
    }
    return py::array_t<float>(result.size(), &result[0]);
}

double mean_dist(py::array_t<float, 1> &dist, double vmin, double vmax){
    auto dist_uc = dist.unchecked<1>();
    int bins = dist.size();

    double delta = (vmax - vmin) / bins;

    double mean, m2;
    mean = m2 = 0;

    double center = vmin + 0.5 * delta;
    for(int b=0; b < bins; ++b){
        mean += center * dist_uc(b);
        center += delta;
    }

    return mean;
}

std::array<double, 2> mean_variance_dist(py::array_t<float, 1> &dist, double vmin, double vmax){
    auto dist_uc = dist.unchecked<1>();
    int bins = dist.size();

    double delta = (vmax - vmin) / bins;

    double mean, m2;
    mean = m2 = 0;

    double center = vmin + 0.5 * delta;
    for(int b=0; b < bins; ++b){
        double tmp = center * dist_uc(b);
        mean += tmp;
        m2 += center * tmp;
        center += delta;
    }
    double var = m2 - mean * mean;

    std::array<double, 2> result = {mean, var};

    return result;
}



#endif
