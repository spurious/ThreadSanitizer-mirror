class Vector {
   std::vector<double> k;
   int N;
public:
   explicit Vector(int size): k(size) {      
      N = size;
      for (int i = 0; i < N; i++)
         k[i] = 0.0;
   }
   
   inline int GetSize() const { return N; }
   
   inline double& operator[] (int ix) {
      CHECK(ix < N && ix >= 0);
      return k[ix];
   }
   
   inline const double& operator[] (int ix) const {
      CHECK(ix < N && ix >= 0);
      return k[ix];
   }
   
   inline Vector operator+ (const Vector & other) const {
      CHECK(other.GetSize() == N);
      Vector ret(N);
      for (int i = 0; i < N; i++)
         ret[i] = k[i] + other[i];
      return ret;
   }
   
   inline Vector operator- (const Vector & other) const {
      CHECK(other.GetSize() == N);
      Vector ret(N);
      for (int i = 0; i < N; i++)
         ret[i] = k[i] - other[i];
      return ret;
   }

   inline Vector operator* (double factor) const {
      Vector ret(N);
      for (int i = 0; i < N; i++)
         ret[i] = k[i] * factor;
      return ret;
   }
   
   inline double DotProduct (const Vector & other) const {
      CHECK(other.GetSize() == N);
      double ret = 0.0;
      for (int i = 0; i < N; i++)
         ret += k[i] * other[i];
      return ret;
   }
   
   inline double Len2 () const {
      double ret = 0.0;
      for (int i = 0; i < N; i++)
         ret += k[i] * k[i];
      return ret;      
   }
   
   inline double Len () const { return sqrt(Len2()); }
   
   string ToString () const {
      char temp[1024] = "(";
      for (int i = 0; i < N; i++)
         sprintf(temp, "%s%s%.1lf", temp, i == 0 ? "" : ", ", k[i]);
      return (std::string)temp + ")";
   }
   
   inline void Copy(const Vector & from) {
      CHECK(from.GetSize() == N);
      for (int i = 0; i < N; i++)
         k[i] = from[i];
   }
};

class Matrix {
   int M;
   std::vector<double*> data;
public:
   /*
    * This matrix can grow its "N" i.e. width
    * See http://en.wikipedia.org/wiki/Matrix_(mathematics)
    */
   
   Matrix (int M_, int N) {
      M = M_;
      for (int i = 0; i < N; i++) {
         IncN();
      }
   }
   
   ~Matrix() {
      for (int i = 0; i < data.size(); i++)
         delete [] data[i];
   }

   inline int GetM() const { return M; }
   
   inline int GetN() const { return data.size(); }
   inline void IncN() {
      double * k = new double[M];
      for (int i = 0; i < M; i++)
         k[i] = 0.0;
      data.push_back(k);
   }
   
   
   inline double& At(int i, int j) {
      CHECK(i < M && i >= 0);
      CHECK(j < data.size() && j >= 0);
      // Note the reverse order of indices!
      return data[j][i];
   }
   
   inline const double& At(int i, int j) const {
      CHECK(i < M && i >= 0);
      CHECK(j < data.size() && j >= 0);
      // Note the reverse order of indices!
      return data[j][i];
   }
   
   Vector MultiplyRight (const Vector & v) const {
      int N = data.size();
      CHECK(v.GetSize() == N);
      Vector ret(M);
      for (int i = 0; i < M; i++)
         for (int j = 0; j < N; j++)
            ret[i] += v[j] * At(i,j);
      return ret;
   }
   
   Vector MultiplyLeft (const Vector & v_to_transpose) const {
      int N = data.size();
      CHECK(v_to_transpose.GetSize() == M);
      Vector ret(N);
      for (int i = 0; i < M; i++)
         for (int j = 0; j < N; j++)
            ret[j] += v_to_transpose[i] * At(i,j);
      return ret;      
   }
   
   string ToString() const {
      string ret = "";
      for (int i = 0; i < M; i++) {
         ret += "[";
         for (int j = 0; j < GetN(); j++) {
            char temp[128] = "";
            sprintf(temp, "%s%.1lf", j == 0 ? "" : ", ", At(i,j));
            ret += temp;
         }
         ret += "]\n";
      }
      return ret;
   }
};

Vector EstimateParameters(Matrix & perf_m, Vector & stats_v, double rel_diff)
{
   /*
    *  Goal: find Vector<N> parameters:
    *   (perf_m * parameters - stats_v)^2 -> min
    * With the following limitations: 
    *    parameters[i] >= 0 
    * "Stop rule":
    *    for every i=0...M 
    *    -> |stats_v[i] - (perf_m * parameters)[i]| < rel_diff * stats_v[i]
    * Algorithm used is a gradient descend
    */
   int N = perf_m.GetN(), M = perf_m.GetM();
   CHECK(stats_v.GetSize() == M);
   Vector current(N);
   bool stop_condition = false;
   double prev_distance = stats_v.Len2();
   //printf("Initial distance = %.1lf\n", prev_distance); 
      
   while (stop_condition == false) {
      Vector m_by_curr(M);
      m_by_curr.Copy(perf_m.MultiplyRight(current));
      Vector derivative(N); // this is actually "-derivative/2" :-)
      derivative.Copy(perf_m.MultiplyLeft(stats_v - m_by_curr));

      if (derivative.Len() > 1000.0) {
         derivative = derivative * (1000.0 / derivative.Len());
      }

      // Descend according to the gradient
      {
         Vector new_curr(N);
         double step = 1024.0;
         double new_distance;
         for (int i = 0; i < N; i++) {
            if (current[i] + derivative[i] * step < 0.0) {
               step = - current[i] / derivative[i];
            }
         }
         do {
            new_curr = current + derivative*step;
            step /= 2.0;
            new_distance = (perf_m.MultiplyRight(new_curr) - stats_v).Len2();
            if (step < 0.00001) {
               stop_condition = true;
            }
         } while (new_distance >= prev_distance && !stop_condition);
         
         prev_distance = new_distance;
         current = new_curr;
         //printf ("Dist=%.1lf, cur=%s\n", new_distance, current.ToString().c_str());
      }
      
      // Check stop condition
      if (stop_condition == false)
      {
         stop_condition = true;
         Vector stats_est(M);
         stats_est.Copy(perf_m.MultiplyRight(current));
         for (int i = 0; i < M; i++)
            if (fabs(stats_v[i] - stats_est[i]) > rel_diff * stats_v[i])
               stop_condition = false;
      }
   }
   
   return current;
}
