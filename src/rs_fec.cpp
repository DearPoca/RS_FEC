#include "rs_fec.h"

#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace poca {
#define MAX_DATA_LENGTH 4096
    void print_matrix(std::vector<std::vector<gf2_8>> &m) {
        for (int i = 0; i < m.size(); ++i) {
            for (int j = 0; j < m[i].size(); ++j) std::cout << std::setw(3) << std::setfill(' ') << int(m[i][j]) << " ";
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }

    std::vector<std::vector<gf2_8>> matrix_multi(std::vector<std::vector<gf2_8>> &a,
                                                 std::vector<std::vector<gf2_8>> &b) {
        int na = a.size();
        int nb = b.size();
        if (na == 0 || nb == 0) return {};
        int ma = a[0].size();
        int mb = b[0].size();
        if (ma != nb) return {};

        std::vector<std::vector<gf2_8>> ret(na, std::vector<gf2_8>(mb, 0));
        for (int i = 0; i < na; ++i) {
            for (int j = 0; j < mb; ++j) {
                for (int k = 0; k < ma; ++k) {
                    ret[i][j] = gf_2_8_add(ret[i][j], gf_2_8_multi(a[i][k], b[k][j]));
                }
            }
        }
        return ret;
    }

    std::vector<std::vector<gf2_8>> &init_base_matrix(int n, int m) {
        static std::mutex base_matrix_mux;
        static std::map<std::pair<int, int>, std::vector<std::vector<gf2_8>>> n_m_to_base_matrix;

        std::lock_guard<std::mutex> lock(base_matrix_mux);
        if (n_m_to_base_matrix.count(std::pair<int, int>(n, m))) {
            return n_m_to_base_matrix[std::pair<int, int>(n, m)];
        }
        n_m_to_base_matrix[std::pair<int, int>(n, m)] =
            std::vector<std::vector<gf2_8>>(m + n, std::vector<gf2_8>(n, 0));
        auto &ret = n_m_to_base_matrix[std::pair<int, int>(n, m)];
        for (int i = 0; i < n; ++i) {
            ret[i][i] = 1;
        }
        for (int i = 0; i < m; ++i) {
            for (int j = 0; j < n; ++j) {
#if USE_CAUCHY_MATRIX
                const int cauchy_x_base = 1;
                const int cauchy_y_base = 128;
                ret[n + i][j] = gf_2_8_div(1, gf_2_8_add(i + cauchy_x_base, j + cauchy_y_base));
#else
                ret[n + i][j] = gf_2_8_power(i + 1, j);
#endif
            }
        }
        return ret;
    }

    int rs_fec_encode(uint8_t ***redundance, uint8_t **src, int size, int n, int m) {
        if (m > n) return -1;

        init_galois_field();
        auto &matrix = init_base_matrix(n, m);

        auto init_encode_buffer = [&]() {
            static std::map<int, uint8_t **> m_to_encode_buffer;
            static std::mutex encode_init_mux;
            std::lock_guard<std::mutex> lock(encode_init_mux);
            if (m_to_encode_buffer.count(m)) {
                return m_to_encode_buffer[m];
            }
            m_to_encode_buffer[m] = new uint8_t *[m];
            for (int i = 0; i < m; i++) {
                m_to_encode_buffer[m][i] = new uint8_t[MAX_DATA_LENGTH];
            }
            return m_to_encode_buffer[m];
        };

        uint8_t **ret = init_encode_buffer();
        *redundance = ret;
        for (int i = 0; i < m; i++) {
            for (int j = 0; j < size; ++j) {
                ret[i][j] = 0;
                for (int k = 0; k < n; ++k) {
                    ret[i][j] = gf_2_8_add(ret[i][j], gf_2_8_multi(matrix[n + i][k], src[k][j]));
                }
            }
            ret[i][size] = 0;
        }
        return 0;
    }

    std::vector<std::vector<gf2_8>> find_inverse_matrix(std::vector<std::vector<gf2_8>> src) {
        int n = src.size();
        for (int i = 0; i < n; ++i)
            if (src[i].size() != n) return {};
        std::vector<std::vector<gf2_8>> ret(n, std::vector<gf2_8>(n, 0));
        for (int i = 0; i < n; ++i) ret[i][i] = 1;
        auto add_line = [&](int i, int j) {
            gf2_8 tmp;
            for (int k = 0; k < n; ++k) {
                src[i][k] = gf_2_8_add(src[i][k], src[j][k]);
            }
            for (int k = 0; k < n; ++k) {
                ret[i][k] = gf_2_8_add(ret[i][k], ret[j][k]);
            }
        };
        auto find_vec = [&](int i) -> int {
            for (int j = i; j < n; ++j)
                if (src[j][i] != 0) return j;
            return -1;
        };
        for (int i = 0; i < n; ++i) {
            int st = find_vec(i);
            if (st == -1) return {};
            if (st != i) add_line(i, st);
            gf2_8 div = gf_2_8_div(src[i][i], 1);
            for (int r = 0; r < n; ++r) {
                src[i][r] = gf_2_8_div(src[i][r], div);
            }
            for (int r = 0; r < n; ++r) {
                ret[i][r] = gf_2_8_div(ret[i][r], div);
            }
            for (int j = i + 1; j < n; ++j) {
                gf2_8 mul = gf_2_8_div(src[j][i], src[i][i]);
                for (int r = 0; r < n; ++r) {
                    src[j][r] = gf_2_8_sub(src[j][r], gf_2_8_multi(src[i][r], mul));
                }
                for (int r = 0; r < n; ++r) {
                    ret[j][r] = gf_2_8_sub(ret[j][r], gf_2_8_multi(ret[i][r], mul));
                }
            }
        }
        for (int i = n - 1; i > 0; --i) {
            for (int j = i - 1; j >= 0; --j) {
                gf2_8 mul = gf_2_8_div(src[j][i], src[i][i]);
                src[j][i] = gf_2_8_sub(src[j][i], gf_2_8_multi(src[i][i], mul));
                for (int k = 0; k < n; ++k) {
                    ret[j][k] = gf_2_8_sub(ret[j][k], gf_2_8_multi(ret[i][k], mul));
                }
            }
        }

#if LOG_PRINT_INVERSE_MATRIX
        std::cout << "Src matrix after inverse: " << std::endl;
        print_matrix(src);
        std::cout << "Inverse matrix: " << std::endl;
        print_matrix(ret);
#endif
        return ret;
    }

    std::vector<std::vector<gf2_8>> find_recov_matrix(int n, int m, const std::vector<int> &indexes) {
        if (indexes.size() < n) return {};
        auto &base_matrix = init_base_matrix(n, m);
        std::vector<std::vector<gf2_8>> src;
        for (int i = 0; i < n; ++i) {
            src.push_back(base_matrix[indexes[i]]);
        }
        return find_inverse_matrix(src);
    }

    int rs_fec_decode(uint8_t ***dst, uint8_t **src, int size, int n, int m) {
        init_galois_field();
        std::vector<int> indexes;
        for (int i = 0; i < n + m && indexes.size() < n; ++i) {
            if (src[i] != nullptr) indexes.push_back(i);
        }

        auto recov_matirx = find_recov_matrix(n, m, indexes);
        if (recov_matirx.size() != n) return -1;
        auto init_decode_buffer = [&]() {
            static std::map<int, uint8_t **> n_to_decode_buffer;
            static std::mutex decode_init_mux;
            std::lock_guard<std::mutex> lock(decode_init_mux);
            if (n_to_decode_buffer.count(n)) {
                return n_to_decode_buffer[n];
            }
            n_to_decode_buffer[n] = new uint8_t *[n];
            for (int i = 0; i < n; i++) {
                n_to_decode_buffer[n][i] = new uint8_t[MAX_DATA_LENGTH];
            }
            return n_to_decode_buffer[n];
        };
        uint8_t **ret = init_decode_buffer();
        *dst = ret;

        auto check_and_copy = [&](int i) {
            int x = -1;
            for (int j = 0; j < n; ++j) {
                if (recov_matirx[i][j] == 1) {
                    if (x == -1) x = j;
                } else
                    return false;
            }
            if (x == -1) return false;
            ret[i] = src[x];
            return true;
        };

        for (int i = 0; i < n; ++i) {
            if (!check_and_copy(i)) {
                for (int r = 0; r < size; ++r) {
                    ret[i][r] = 0;
                    for (int j = 0; j < n; ++j) {
                        ret[i][r] = gf_2_8_add(ret[i][r], gf_2_8_multi(recov_matirx[i][j], src[indexes[j]][r]));
                    }
                }
            }
            ret[i][size] = 0;
        }
        return 0;
    }

}  // namespace poca