#include<math.h>
#include<vector>
#include<stdio.h>
#include<stdlib.h>
#include<omp.h>
#include<iostream>
#include<fstream>
#include<string>
#include<time.h>
using namespace std;

double diffclock(clock_t clock1, clock_t clock2) {
	double diffticks = clock1 - clock2;
	double diffms = (diffticks * 1000) / CLOCKS_PER_SEC;
	return diffms;
}

// Initialize number of documents, topics and words in vocabulary
unsigned int W, D, K;

int main(int argc, char* argv[]) {
	if (argc < 4) {
		printf("Usage: ./fastLDA inputfile num_iterations num_topics\n");
		return 1;
	}

	// Initlialize expected topic counts per document
	float **nTheta;
	// Dynamically
	float **nPhi;
	float *N_z;
	// Initialize estimates from each minibatch
	// Initialize step sizes
	float rhoTheta = 0;
	float rhoPhi = 0;
	float *gamma_ij;
	float **phi;
	float **theta;
	float *perplexities;
	int cnt;
	// Initlalize dirichlet prior parameters
	float alpha, eta;
	float M; // Number of documents in each minibatch
	int j, Cj = 0, i, k, w;
	double norm_sum = 0;
	int batch_idx = 0;
	int C = 0;
	int MAXITER;
	int iter = 0;
	unsigned int i_cnt = 0;
	int NNZ;
	float perplexityval, innerval;
	time_t begin, end;
	int m_aj;
	ofstream pfile;
	pfile.open("perplexity.txt");

	M = 100; //343 works for KOS and only for KOS
	eta = 0.01; // was 0.01
	alpha = 0.1;

	//if user also specified a minibatch size
	if (argc == 5 || argc == 6) {
		M = atof(argv[4]);
	}

	MAXITER = atoi(argv[2]);
	K = atoi(argv[3]);

	printf("Input file: %s\n", argv[1]);
	printf("Number of iterations: %d\n", MAXITER);
	printf("Number of topics: %d\n", K);
	printf("Minibatch size: %f\n", M);
	printf("alpha:  %f\n", alpha);
	printf("eta:  %f\n", eta);

	// Read the file and store it in DATA
	FILE* fptr;
	unsigned int docnum, wnum;
	unsigned char countnum;

	fptr = fopen(argv[1], "rt");

	fscanf(fptr, "%d\n", &D);
	fscanf(fptr, "%d\n", &W);
	fscanf(fptr, "%d\n", &NNZ);

	printf("Number of documents: %d\n", D);
	printf("Vocabulary size: %d\n", W);

	// Dynamically allocate phi
	phi = new float*[W];
//#pragma omp parallel for
	for (w = 0; w < W; w++) {
		phi[w] = new float[K];
	}

	printf("allocated phi\n");

	// Dynamically allocate theta

	theta = new float*[D];
//#pragma omp parallel for
	for (i = 0; i < D; i++) {
		theta[i] = new float[K];
	}

	printf("allocated theta\n");

	vector<vector<int> > corpus;
	vector<int> corpus_size(D, 0);
	corpus.resize(D);
	vector<vector<int> > corpus_expanded;
	corpus_expanded.resize(D);

	while (!feof(fptr)) {
		fscanf(fptr, "%d %d %hhu\n", &docnum, &wnum, &countnum);

		corpus[docnum - 1].push_back(wnum - 1);
		corpus[docnum - 1].push_back(countnum);

		corpus_size[docnum - 1] += countnum;

		for (i = 0; i < countnum; i++) {
			corpus_expanded[docnum - 1].push_back(wnum - 1);
		}
	}
	fclose(fptr);

	// Initialize phi_est and all other arrays
	nPhi = new float*[W];

	for (i = 0; i < W; i++) {
		nPhi[i] = new float[K];
	}

	for (i = 0; i < W; i++) {
		for (k = 0; k < K; k++) {
			nPhi[i][k] = rand() % 10;
		}
	}

	// Initialize n_z and n_z_est and other arrays
	N_z = new float[K];
	for (k = 0; k < K; k++) {
		N_z[k] = 0;
	}

	//if parallelizing this, make sure to avoid race condition (most likely use reduction)
	for (k = 0; k < K; k++) {
		for (w = 0; w < W; w++) {
			N_z[k] += nPhi[w][k];
		}
	}

	perplexities = new float[MAXITER];
	for (i = 0; i < MAXITER; i++) {
		perplexities[i] = 0;
	}

	nTheta = new float*[D];
	for (i = 0; i < D; i++) {
		nTheta[i] = new float[K];
	}

	for (i = 0; i < D; i++) {
		for (k = 0; k < K; k++) {
			nTheta[i][k] = rand() % 10;
		}
	}

	// Find the total number of word in the document
	for (j = 0; j < D; j++) {
		C += corpus_size[j];
	}

	printf("Number of words in corpus: %d\n", C);

	int firstdoc = 0;
	int lastdoc = 0;
	int DM = D / M;

	for (iter = 0; iter < MAXITER; iter++) {
		// Decide rho_phi and rho_theta
		rhoPhi = 10 / pow((1000 + iter), 0.9);
		rhoTheta = 1 / pow((10 + iter), 0.9);

#pragma omp parallel private(batch_idx,j,k,norm_sum,i,w,firstdoc,lastdoc)
		{
			float *gamma = new float[K];
			float *nzHat = new float[K];
			float **nPhiHat = new float *[W];
			for (k = 0; k < K; k++) {
				gamma[k] = 0;
				nzHat[k] = 0;
			}
			for (i = 0; i < W; i++) {
				nPhiHat[i] = new float[K];
				for (k = 0; k < K; k++) {
					nPhiHat[i][k] = 0;
				}
			}

#pragma omp for
			for (batch_idx = 0; batch_idx < DM; batch_idx++) {

				// Decide the document indices which go in each minibatch
				firstdoc = batch_idx * M;
				lastdoc = (batch_idx + 1) * M;

				for (j = firstdoc; j < lastdoc; j++) {

					// First perform the burn-in passes
					// Iteration of burn in passes

					// Store size of corpus in Cj
					Cj = corpus_size[j];

					for (i = 0; i < (corpus[j].size() / 2); i++) {// indexing is very different here!

						int w_aj = corpus[j][2 * i];
						int m_aj = corpus[j][(2 * i) + 1];
						// Update gamma_ij and N_theta
						float norm_sum = 0;

						for (k = 0; k < K; k++) {
							gamma[k] = (nPhi[w_aj][k] + eta) * (nTheta[j][k] + alpha) / (N_z[k] + (eta * W));
							norm_sum += gamma[k];
						}

						for (k = 0; k < K; k++) {
							gamma[k] = gamma[k] / norm_sum;
						}

						for (k = 0; k < K; k++) {

							nTheta[j][k] = (pow((1 - rhoTheta), m_aj) * nTheta[j][k])
									+ ((1 - pow((1 - rhoTheta), m_aj)) * Cj * gamma[k]);
						}

					}

					// Iteration of the main loop
					i_cnt = 0;

					for (i = 0; i < (corpus[j].size() / 2); i++) { // indexing is very different here!

						int w_aj = corpus[j][2 * i];
						int m_aj = corpus[j][(2 * i) + 1];
						norm_sum = 0;
						for (k = 0; k < K; k++) {
							gamma[k] = (nPhi[w_aj][k] + eta) * (nTheta[j][k] + alpha) / (N_z[k] + (eta * W));
							norm_sum += gamma[k];
						}

						for (k = 0; k < K; k++) {
							gamma[k] = gamma[k] / norm_sum;
						}

						// Update N_theta estimates
						for (k = 0; k < K; k++) {
							nTheta[j][k] = (pow((1 - rhoTheta), m_aj) * nTheta[j][k])
									+ ((1 - pow((1 - rhoTheta), m_aj)) * Cj * gamma[k]);

							nPhiHat[w_aj][k] = nPhiHat[w_aj][k] + (C * gamma[k] / M);

							nzHat[k] = nzHat[k] + (C * gamma[k] / M);
						}
					}

				} // End of j

				// Update the estimates matrix
				for (k = 0; k < K; k++) {
					for (w = 0; w < W; w++) {
						nPhi[w][k] = (1 - rhoPhi) * nPhi[w][k]
								+ rhoPhi * nPhiHat[w][k];
					}
#pragma omp atomic
					N_z[k] *= (1 - rhoPhi);
#pragma omp atomic
					N_z[k] += rhoPhi * nzHat[k];
				}

			} // End of batch_idx

			// Compute phi
#pragma omp for
			for (k = 0; k < K; k++) {
				norm_sum = 0;
				for (w = 0; w < W; w++) {
					nPhi[w][k] += eta;
					norm_sum += nPhi[w][k];
				}
				for (w = 0; w < W; w++) {
					phi[w][k] = (float) nPhi[w][k] / norm_sum;
				}
			}

			// Compute theta
#pragma omp for
			for (i = 0; i < D; i++) {
				norm_sum = 0;
				for (k = 0; k < K; k++) {
					nTheta[i][k] += alpha;
					norm_sum += nTheta[i][k];
				}
				for (k = 0; k < K; k++) {
					theta[i][k] = (float) nTheta[i][k] / norm_sum;
				}
			}

			delete[] gamma;
			delete[] nzHat;

			for (i = 0; i < W; i++) {
				delete[] nPhiHat[i];
			}

			delete[] nPhiHat;

		}

		// Calculate the perplexity here
		// Compute posterior means here
		// Iterate over the corpus here
		perplexityval = 0;
#pragma omp parallel for private(j,i,k) reduction(+:innerval) reduction(+:perplexityval)
		for (j = 0; j < D; j++) {
			for (i = 0; i < corpus_expanded[j].size(); i++) {
				innerval = 0;
				for (k = 0; k < K; k++) {
					innerval += (theta[j][k] * phi[corpus_expanded[j][i]][k]);
				}
				perplexityval += (log(innerval) / log(2));
			}
		}
		printf("%d,%f\n", iter, pow(2, -perplexityval / C));
		perplexities[iter] = pow(2, -perplexityval / C);

		pfile << iter + 1 << "," << perplexities[iter] << endl;
		pfile.flush();

	} // End of iter

	//write doctopics file
	ofstream dtfile;
	dtfile.open("doctopics.txt");
	for (i = 0; i < D; i++) {
		for (k = 0; k < K; k++) {
			dtfile << theta[i][k] << ",";
		}
		dtfile << endl;
	}
	dtfile.close();

	//compute the top 100 words for each topic
	int** topwords;
	float** maxval;
	topwords = new int*[K];
	maxval = new float*[K];
	for (k = 0; k < K; k++) {
		topwords[k] = new int[100];
		maxval[k] = new float[100];
	}

	for (k = 0; k < K; k++) {
		for (i = 0; i < 100; i++) {
			float max = -1;
			int max_idx = -1;
			for (w = 0; w < W; w++) {
				if (phi[w][k] > max) {
					max = phi[w][k];
					max_idx = w;
				}
			}
			phi[max_idx][k] = 0;
			topwords[k][i] = max_idx;
			maxval[k][i] = max;
		}
	}

	string *dict;
	dict = new string[W];
//	char word;
	//retrieve the words from the file
	w = 0;
	string line;
	ifstream myfile(argv[5]);
	if (myfile.is_open()) {
		while (getline(myfile, line)) {
			dict[w] = line;
			w++;
		}
		myfile.close();
	}
//	while (!feof(fptr)) {
//		fscanf(fptr, "%s\n", &word);
//		dict[w] = word;
//		w++;
//		printf("%d", w);
//	}
	fclose(fptr);
	//write topics file
	ofstream tfile;
	tfile.open("topics.txt");
	for (k = 0; k < K; k++) {
		for (w = 0; w < 100; w++) {
			tfile << topwords[k][w] << ":" << maxval[k][w] << ",";

		}
		tfile << endl;

		for (w = 0; w < 100; w++) {
			tfile << dict[topwords[k][w]] << ",";
		}

		tfile << endl;
	}
	tfile.close();

	return (0);

} // End of main
